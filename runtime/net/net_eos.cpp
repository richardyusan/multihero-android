// SPDX-License-Identifier: MIT
// net_eos.cpp - Epic Online Services backend: Connect (device id login, no Epic account),
// Lobby (room list / create / join / ready / start) and P2P (ping + game data, NAT traversal
// and Epic relays included).
//
// Compiled only with -DMH_WITH_EOS (build_android.sh adds it when the EOS SDK is found in
// third_party/eos). Without it net_create_eos() returns nullptr and net.cpp uses the demo.
//
// Credentials come from online.cfg / online.local.cfg:
//   eos_product_id, eos_sandbox_id, eos_deployment_id, eos_client_id, eos_client_secret
//
// Lobby data used by MultiHero
//   bucket id          "multihero:1"                 (search filter, bump when the protocol changes)
//   lobby attributes   ROOMNAME, HOSTNAME, STARTED (0/1), SEED
//   member attributes  NAME, READY (0/1), PING (ms to the lobby owner, -1 for the owner)
//   P2P socket         "MHGAME": channel 0 = ping ("P<ms>" / "Q<ms>"), channel 1 = game data
#include "net.h"

#ifndef MH_WITH_EOS

NetBackend *net_create_eos() { return nullptr; }

#else

#include "../platform.h"
#include <eos_sdk.h>
#include <eos_logging.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include <eos_p2p.h>
#ifdef __ANDROID__
#include <Android/eos_Android.h>
#endif

#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <algorithm>
#include <set>
#include <vector>
#include <sys/stat.h>

extern void (*g_net_background_tick)();

namespace {

const char *BUCKET = "multihero:1";
const char *SOCKET_NAME = "MHGAME";
const uint8_t CH_PING = 0, CH_GAME = 1;

// EOS can be initialised only once per process, so the platform and the login outlive the
// backend object (which is created/destroyed every time the lobby screen opens/closes).
EOS_HPlatform g_platform = nullptr;
EOS_ProductUserId g_user = nullptr;
std::string g_init_error;

std::string uid_str(EOS_ProductUserId id) {
  if (!id) return std::string();
  char buf[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
  int32_t len = sizeof buf;
  if (EOS_ProductUserId_ToString(id, buf, &len) != EOS_EResult::EOS_Success) return std::string();
  return buf;
}
std::string res_str(EOS_EResult r) { return EOS_EResult_ToString(r); }

// Release EOS before the process exits: its P2P threads (WebRTC) must stop before OpenSSL's
// own exit handler runs, otherwise they abort while creating certificates.
void shutdown_platform() {
  if (!g_platform) return;
  g_net_background_tick = nullptr;
  EOS_Platform_Release(g_platform);
  g_platform = nullptr;
  g_user = nullptr;
  EOS_Shutdown();
}

void tick_platform() {
  if (g_platform) EOS_Platform_Tick(g_platform);
}

void EOS_CALL log_cb(const EOS_LogMessage *m) {
  char buf[600];
  snprintf(buf, sizeof buf, "EOS[%s] %s", m->Category, m->Message);
  plat_log(buf);
}

bool init_platform(const NetConfig &cfg) {
  if (g_platform) return true;
  static bool tried = false;
  if (tried) return false;
  tried = true;

  EOS_InitializeOptions io = {};
  io.ApiVersion = EOS_INITIALIZE_API_LATEST;
  io.ProductName = "MultiHero";
  io.ProductVersion = "0.96-android1.7";
  std::string dir = plat_data_dir() + "eos";
  MH_MKDIR(dir.c_str(), 0700);
#ifdef __ANDROID__
  static EOS_Android_InitializeOptions ao = {};
  static std::string sdir;
  sdir = dir;
  ao.ApiVersion = EOS_ANDROID_INITIALIZEOPTIONS_API_LATEST;
  ao.Reserved = nullptr;
  ao.OptionalInternalDirectory = sdir.c_str();
  ao.OptionalExternalDirectory = sdir.c_str();
  io.SystemInitializeOptions = &ao;
#endif
  EOS_EResult r = EOS_Initialize(&io);
  if (r != EOS_EResult::EOS_Success && r != EOS_EResult::EOS_AlreadyConfigured) {
    g_init_error = "eos init failed: " + res_str(r);
    return false;
  }
  EOS_Logging_SetCallback(log_cb);
  EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Warning);

  static std::string pid, sid, did, cid, csec, cache;
  pid = cfg.get("eos_product_id");
  sid = cfg.get("eos_sandbox_id");
  did = cfg.get("eos_deployment_id");
  cid = cfg.get("eos_client_id");
  csec = cfg.get("eos_client_secret");
  cache = dir + "/cache";
  MH_MKDIR(cache.c_str(), 0700);
  if (pid.empty() || sid.empty() || did.empty() || cid.empty() || csec.empty()) {
    g_init_error = "eos credentials missing in online.cfg";
    return false;
  }
  EOS_Platform_Options po = {};
  po.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
  po.ProductId = pid.c_str();
  po.SandboxId = sid.c_str();
  po.DeploymentId = did.c_str();
  po.ClientCredentials.ClientId = cid.c_str();
  po.ClientCredentials.ClientSecret = csec.c_str();
  po.bIsServer = EOS_FALSE;
  po.CacheDirectory = cache.c_str();
  po.Flags = EOS_PF_DISABLE_OVERLAY;
  static double timeout = 10.0;
  po.TaskNetworkTimeoutSeconds = &timeout;
  g_platform = EOS_Platform_Create(&po);
  if (!g_platform) {
    g_init_error = "eos platform create failed (check the credentials)";
    return false;
  }
  EOS_Platform_SetNetworkStatus(g_platform, EOS_ENetworkStatus::EOS_NS_Online);
  g_net_background_tick = tick_platform;
  atexit(shutdown_platform);
  return true;
}

class NetEos : public NetBackend {
 public:
  const char *name() const override { return "epic online"; }

  ~NetEos() override { disconnect(); alive_set().erase(this); }

  void connect() override {
    state = NET_CONNECTING;
    lastError.clear();
    if (!init_platform(cfg)) {
      state = NET_ERROR;
      lastError = g_init_error;
      return;
    }
    lobby_ = EOS_Platform_GetLobbyInterface(g_platform);
    p2p_ = EOS_Platform_GetP2PInterface(g_platform);
    connect_ = EOS_Platform_GetConnectInterface(g_platform);
    if (g_user) { onLoggedIn(); return; }
    // device id: an anonymous id tied to this install, no Epic account needed
    EOS_Connect_CreateDeviceIdOptions o = {};
    o.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
#ifdef __ANDROID__
    o.DeviceModel = "Android";
#else
    o.DeviceModel = "PC";
#endif
    EOS_Connect_CreateDeviceId(connect_, &o, this, [](const EOS_Connect_CreateDeviceIdCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      if (d->ResultCode == EOS_EResult::EOS_Success || d->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
        self->login();
      else self->fail("device id: " + res_str(d->ResultCode));
    });
  }

  void disconnect() override {
    if (!lobby_ || !g_platform) { lobby_ = nullptr; state = NET_OFFLINE; return; }
    if (!roomId.empty()) leaveRoom();
    removeNotifies();
    clearSearch();
    if (p2p_ && g_user) {
      EOS_P2P_CloseConnectionsOptions o = {};
      o.ApiVersion = EOS_P2P_CLOSECONNECTIONS_API_LATEST;
      o.LocalUserId = g_user;
      o.SocketId = &socket_;
      EOS_P2P_CloseConnections(p2p_, &o);
    }
    alive_set().erase(this);
    lobby_ = nullptr;
    state = NET_OFFLINE;
  }

  void update(int64_t now) override {
    if (!g_platform) return;
    EOS_Platform_Tick(g_platform);
    if (!g_user || !lobby_) return;
    receivePackets(now);
    tickCountdown(now);
    if (inRoom()) {
      if (now >= nextPing_) { sendPing(now); nextPing_ = now + 2000; }
      flushUpdates(now);
      if (dirtyMembers_) rebuildRoom();
    } else if (state == NET_ONLINE && now >= nextSearch_) {
      refreshRooms();
    }
  }

  void refreshRooms() override {
    if (!g_user || searching_) return;
    nextSearch_ = net_now_ms() + 4000;
    EOS_HLobbySearch search = nullptr;
    EOS_Lobby_CreateLobbySearchOptions co = {};
    co.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
    co.MaxResults = 20;
    if (EOS_Lobby_CreateLobbySearch(lobby_, &co, &search) != EOS_EResult::EOS_Success) return;
    EOS_Lobby_AttributeData ad = {};
    ad.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    ad.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
    ad.Value.AsUtf8 = BUCKET;
    ad.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    EOS_LobbySearch_SetParameterOptions sp = {};
    sp.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
    sp.Parameter = &ad;
    sp.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
    EOS_LobbySearch_SetParameter(search, &sp);
    EOS_LobbySearch_FindOptions fo = {};
    fo.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
    fo.LocalUserId = g_user;
    searching_ = search;
    EOS_LobbySearch_Find(search, &fo, this, [](const EOS_LobbySearch_FindCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      self->onSearch(d->ResultCode);
    });
  }

  void createRoom(const std::string &roomName, int maxPlayers) override {
    if (state != NET_ONLINE) return;
    pendingRoomName_ = roomName;
    EOS_Lobby_CreateLobbyOptions o = {};
    o.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    o.LocalUserId = g_user;
    o.MaxLobbyMembers = (uint32_t)maxPlayers;
    o.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    o.bPresenceEnabled = EOS_FALSE;
    o.bAllowInvites = EOS_TRUE;
    o.BucketId = BUCKET;
    o.bDisableHostMigration = EOS_FALSE;
    o.bEnableRTCRoom = EOS_FALSE;
    o.bEnableJoinById = EOS_TRUE;
    state = NET_CONNECTING;
    EOS_Lobby_CreateLobby(lobby_, &o, this, [](const EOS_Lobby_CreateLobbyCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      if (d->ResultCode != EOS_EResult::EOS_Success) {
        self->state = NET_ONLINE;
        self->lastError = "create room: " + res_str(d->ResultCode);
        return;
      }
      self->enterRoom(d->LobbyId, true);
    });
  }

  void joinRoom(const std::string &id) override {
    if (state != NET_ONLINE) return;
    auto it = details_.find(id);
    if (it == details_.end()) { lastError = "room not found, refresh"; return; }
    EOS_Lobby_JoinLobbyOptions o = {};
    o.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    o.LobbyDetailsHandle = it->second;
    o.LocalUserId = g_user;
    o.bPresenceEnabled = EOS_FALSE;
    state = NET_CONNECTING;
    EOS_Lobby_JoinLobby(lobby_, &o, this, [](const EOS_Lobby_JoinLobbyCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      if (d->ResultCode != EOS_EResult::EOS_Success) {
        self->state = NET_ONLINE;
        self->lastError = "join room: " + res_str(d->ResultCode);
        self->refreshRooms();
        return;
      }
      self->enterRoom(d->LobbyId, false);
    });
  }

  void leaveRoom() override {
    if (roomId.empty()) return;
    EOS_Lobby_LeaveLobbyOptions o = {};
    o.ApiVersion = EOS_LOBBY_LEAVELOBBY_API_LATEST;
    o.LocalUserId = g_user;
    o.LobbyId = roomId.c_str();
    EOS_Lobby_LeaveLobby(lobby_, &o, nullptr, [](const EOS_Lobby_LeaveLobbyCallbackInfo *) {});
    roomId.clear();
    roomName.clear();
    members.clear();
    ownerId_.clear();
    state = NET_ONLINE;
    nextSearch_ = 0;
  }

  void setReady(bool ready) override {
    myReady_ = ready;
    memberDirty_ = true;
    for (auto &m : members) if (m.me) m.ready = ready;
    flushUpdates(net_now_ms());
  }

  void startMatch() override {
    if (!canStart()) return;
    std::mt19937 rng((uint32_t)net_now_ms());
    int64_t seed = 1 + (int64_t)(rng() % 2000000000u);
    lobbyInt_["SEED"] = seed;
    lobbyInt_["STARTED"] = 1;
    flushUpdates(net_now_ms());
  }

  bool supportsHeroes() const override { return true; }
  void setHero(int hero) override {
    myHero = hero;
    memberDirty_ = true;
    for (auto &m : members) if (m.me) m.hero = hero;
    flushUpdates(net_now_ms());
  }
  void setLives(int lives) override {
    if (!isHost()) return;
    matchLives = lives;
    lobbyInt_["LIVES"] = lives;
    flushUpdates(net_now_ms());
  }
  void setMap(int map) override {
    if (!isHost()) return;
    matchMap = map;
    lobbyInt_["MAP"] = map;
    flushUpdates(net_now_ms());
  }

  void sendGame(const std::string &data) override {
    for (auto &m : members) {
      if (m.me) continue;
      sendTo(m.id, CH_GAME, "G" + data, true);
    }
  }

 private:
  EOS_HLobby lobby_ = nullptr;
  EOS_HP2P p2p_ = nullptr;
  EOS_HConnect connect_ = nullptr;
  EOS_P2P_SocketId socket_ = {EOS_P2P_SOCKETID_API_LATEST, {}};
  EOS_HLobbySearch searching_ = nullptr;
  std::map<std::string, EOS_HLobbyDetails> details_;
  EOS_NotificationId nUpdate_ = EOS_INVALID_NOTIFICATIONID, nMember_ = EOS_INVALID_NOTIFICATIONID,
                     nStatus_ = EOS_INVALID_NOTIFICATIONID, nConnReq_ = EOS_INVALID_NOTIFICATIONID;
  std::string ownerId_, pendingRoomName_;
  bool dirtyMembers_ = false;
  int64_t nextPing_ = 0, nextSearch_ = 0, lastPingAttrMs_ = 0;
  int lastPingAttr_ = -2;
  // our attributes and pending lobby attribute changes (see flushUpdates)
  bool myReady_ = false, memberDirty_ = false, updating_ = false;
  int myPing_ = -1;
  int64_t nextUpdate_ = 0;
  std::map<std::string, std::string> lobbyStr_, sentStr_;
  std::map<std::string, int64_t> lobbyInt_, sentInt_;
  std::map<std::string, EOS_ProductUserId> ids_;     // string id -> handle

  // never destroyed: the backend may be deleted by an exit-time destructor after this set would be
  static std::set<NetEos *> &alive_set() { static auto *s = new std::set<NetEos *>; return *s; }
  static bool alive(NetEos *p) { return alive_set().count(p) != 0; }

  void fail(const std::string &e) {
    state = NET_ERROR;
    lastError = e;
  }

  void login() {
    EOS_Connect_Credentials c = {};
    c.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    c.Token = nullptr;
    c.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    static std::string display;
    display = playerName.substr(0, 32);
    EOS_Connect_UserLoginInfo ui = {};
    ui.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    ui.DisplayName = display.c_str();
    EOS_Connect_LoginOptions o = {};
    o.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    o.Credentials = &c;
    o.UserLoginInfo = &ui;
    EOS_Connect_Login(connect_, &o, this, [](const EOS_Connect_LoginCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      if (d->ResultCode == EOS_EResult::EOS_Success) {
        g_user = d->LocalUserId;
        self->onLoggedIn();
      } else if (d->ResultCode == EOS_EResult::EOS_InvalidUser && d->ContinuanceToken) {
        // first start on this device: create the product user
        EOS_Connect_CreateUserOptions co = {};
        co.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        co.ContinuanceToken = d->ContinuanceToken;
        EOS_Connect_CreateUser(self->connect_, &co, self, [](const EOS_Connect_CreateUserCallbackInfo *u) {
          NetEos *s2 = (NetEos *)u->ClientData;
          if (!alive(s2)) return;
          if (u->ResultCode == EOS_EResult::EOS_Success) {
            g_user = u->LocalUserId;
            s2->onLoggedIn();
          } else s2->fail("create user: " + res_str(u->ResultCode));
        });
      } else self->fail("login: " + res_str(d->ResultCode));
    });
  }

  void onLoggedIn() {
    state = NET_ONLINE;
    lastError.clear();
    strncpy(socket_.SocketName, SOCKET_NAME, sizeof socket_.SocketName - 1);
    addNotifies();
    refreshRooms();
  }

  void addNotifies() {
    if (nUpdate_ != EOS_INVALID_NOTIFICATIONID) return;
    EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions a = {EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST};
    nUpdate_ = EOS_Lobby_AddNotifyLobbyUpdateReceived(lobby_, &a, this, [](const EOS_Lobby_LobbyUpdateReceivedCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (alive(self) && self->roomId == d->LobbyId) self->dirtyMembers_ = true;
    });
    EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions b = {EOS_LOBBY_ADDNOTIFYLOBBYMEMBERUPDATERECEIVED_API_LATEST};
    nMember_ = EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(lobby_, &b, this, [](const EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (alive(self) && self->roomId == d->LobbyId) self->dirtyMembers_ = true;
    });
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions c = {EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST};
    nStatus_ = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(lobby_, &c, this, [](const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self) || self->roomId != d->LobbyId) return;
      bool me = d->TargetUserId == g_user;
      auto st = d->CurrentStatus;
      if (me && (st == EOS_ELobbyMemberStatus::EOS_LMS_KICKED || st == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED ||
                 st == EOS_ELobbyMemberStatus::EOS_LMS_LEFT || st == EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED)) {
        self->roomId.clear();
        self->roomName.clear();
        self->members.clear();
        self->state = NET_ONLINE;
        self->lastError = st == EOS_ELobbyMemberStatus::EOS_LMS_CLOSED ? "the room was closed" : "";
        self->nextSearch_ = 0;
        return;
      }
      self->dirtyMembers_ = true;
    });
    // accept P2P connections on our socket (only lobby members know our id)
    EOS_P2P_AddNotifyPeerConnectionRequestOptions p = {};
    p.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    p.LocalUserId = g_user;
    p.SocketId = &socket_;
    nConnReq_ = EOS_P2P_AddNotifyPeerConnectionRequest(p2p_, &p, this, [](const EOS_P2P_OnIncomingConnectionRequestInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      EOS_P2P_AcceptConnectionOptions a = {};
      a.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
      a.LocalUserId = g_user;
      a.RemoteUserId = d->RemoteUserId;
      a.SocketId = &self->socket_;
      EOS_P2P_AcceptConnection(self->p2p_, &a);
    });
  }

  void removeNotifies() {
    if (nUpdate_ != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(lobby_, nUpdate_);
    if (nMember_ != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(lobby_, nMember_);
    if (nStatus_ != EOS_INVALID_NOTIFICATIONID) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(lobby_, nStatus_);
    if (nConnReq_ != EOS_INVALID_NOTIFICATIONID) EOS_P2P_RemoveNotifyPeerConnectionRequest(p2p_, nConnReq_);
    nUpdate_ = nMember_ = nStatus_ = nConnReq_ = EOS_INVALID_NOTIFICATIONID;
  }

  void clearSearch() {
    for (auto &kv : details_) EOS_LobbyDetails_Release(kv.second);
    details_.clear();
    if (searching_) { EOS_LobbySearch_Release(searching_); searching_ = nullptr; }
  }

  // ---- attributes
  static std::string attrStr(EOS_Lobby_Attribute *a) {
    if (!a || !a->Data) return std::string();
    if (a->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && a->Data->Value.AsUtf8) return a->Data->Value.AsUtf8;
    if (a->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_INT64) return std::to_string(a->Data->Value.AsInt64);
    return std::string();
  }
  static std::string lobbyAttr(EOS_HLobbyDetails h, const char *key) {
    EOS_LobbyDetails_CopyAttributeByKeyOptions o = {};
    o.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
    o.AttrKey = key;
    EOS_Lobby_Attribute *a = nullptr;
    if (EOS_LobbyDetails_CopyAttributeByKey(h, &o, &a) != EOS_EResult::EOS_Success) return std::string();
    std::string s = attrStr(a);
    EOS_Lobby_Attribute_Release(a);
    return s;
  }
  static std::string memberAttr(EOS_HLobbyDetails h, EOS_ProductUserId u, const char *key) {
    EOS_LobbyDetails_CopyMemberAttributeByKeyOptions o = {};
    o.ApiVersion = EOS_LOBBYDETAILS_COPYMEMBERATTRIBUTEBYKEY_API_LATEST;
    o.TargetUserId = u;
    o.AttrKey = key;
    EOS_Lobby_Attribute *a = nullptr;
    if (EOS_LobbyDetails_CopyMemberAttributeByKey(h, &o, &a) != EOS_EResult::EOS_Success) return std::string();
    std::string s = attrStr(a);
    EOS_Lobby_Attribute_Release(a);
    return s;
  }

  EOS_HLobbyModification modification() {
    if (roomId.empty()) return nullptr;
    EOS_Lobby_UpdateLobbyModificationOptions o = {};
    o.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
    o.LocalUserId = g_user;
    o.LobbyId = roomId.c_str();
    EOS_HLobbyModification mod = nullptr;
    if (EOS_Lobby_UpdateLobbyModification(lobby_, &o, &mod) != EOS_EResult::EOS_Success) return nullptr;
    return mod;
  }
  static void addAttrStr(EOS_HLobbyModification mod, const char *key, const std::string &v, bool member) {
    EOS_Lobby_AttributeData d = {};
    d.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    d.Key = key;
    d.Value.AsUtf8 = v.c_str();
    d.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    if (member) {
      EOS_LobbyModification_AddMemberAttributeOptions o = {EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST, &d, EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC};
      EOS_LobbyModification_AddMemberAttribute(mod, &o);
    } else {
      EOS_LobbyModification_AddAttributeOptions o = {EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST, &d, EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC};
      EOS_LobbyModification_AddAttribute(mod, &o);
    }
  }
  static void addAttrInt(EOS_HLobbyModification mod, const char *key, int64_t v, bool member) {
    EOS_Lobby_AttributeData d = {};
    d.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    d.Key = key;
    d.Value.AsInt64 = v;
    d.ValueType = EOS_ELobbyAttributeType::EOS_AT_INT64;
    if (member) {
      EOS_LobbyModification_AddMemberAttributeOptions o = {EOS_LOBBYMODIFICATION_ADDMEMBERATTRIBUTE_API_LATEST, &d, EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC};
      EOS_LobbyModification_AddMemberAttribute(mod, &o);
    } else {
      EOS_LobbyModification_AddAttributeOptions o = {EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST, &d, EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC};
      EOS_LobbyModification_AddAttribute(mod, &o);
    }
  }
  // Lobby updates are sent one at a time; each one carries all of our member attributes
  // (NAME, READY, PING) plus any pending lobby attributes, so a late or failed update can
  // never undo a newer value. Failed updates are retried.
  void flushUpdates(int64_t now) {
    if (updating_ || roomId.empty() || now < nextUpdate_) return;
    if (!memberDirty_ && lobbyStr_.empty() && lobbyInt_.empty()) return;
    EOS_HLobbyModification mod = modification();
    if (!mod) { nextUpdate_ = now + 1000; return; }
    addAttrStr(mod, "NAME", playerName, true);
    addAttrInt(mod, "READY", myReady_ ? 1 : 0, true);
    addAttrInt(mod, "PING", myPing_, true);
    addAttrInt(mod, "HERO", myHero, true);
    for (auto &kv : lobbyStr_) addAttrStr(mod, kv.first.c_str(), kv.second, false);
    for (auto &kv : lobbyInt_) addAttrInt(mod, kv.first.c_str(), kv.second, false);
    sentStr_ = lobbyStr_;
    sentInt_ = lobbyInt_;
    lobbyStr_.clear();
    lobbyInt_.clear();
    memberDirty_ = false;
    updating_ = true;
    EOS_Lobby_UpdateLobbyOptions o = {};
    o.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
    o.LobbyModificationHandle = mod;
    EOS_Lobby_UpdateLobby(lobby_, &o, this, [](const EOS_Lobby_UpdateLobbyCallbackInfo *d) {
      NetEos *self = (NetEos *)d->ClientData;
      if (!alive(self)) return;
      self->updating_ = false;
      int64_t now = net_now_ms();
      if (d->ResultCode == EOS_EResult::EOS_Success) {
        self->nextUpdate_ = now + 250;
      } else if (self->roomId == d->LobbyId) {
        plat_log(("EOS lobby update failed, retrying: " + res_str(d->ResultCode)).c_str());
        self->memberDirty_ = true;     // resend; newer pending values win over the failed ones
        for (auto &kv : self->sentStr_) self->lobbyStr_.insert(kv);
        for (auto &kv : self->sentInt_) self->lobbyInt_.insert(kv);
        self->nextUpdate_ = now + 1500;
      }
      self->dirtyMembers_ = true;
    });
    EOS_LobbyModification_Release(mod);
  }

  // ---- search results
  void onSearch(EOS_EResult r) {
    EOS_HLobbySearch search = searching_;
    searching_ = nullptr;
    if (r != EOS_EResult::EOS_Success && r != EOS_EResult::EOS_NotFound) {
      if (search) EOS_LobbySearch_Release(search);
      lastError = "room list: " + res_str(r);
      return;
    }
    for (auto &kv : details_) EOS_LobbyDetails_Release(kv.second);
    details_.clear();
    std::vector<NetRoom> list;
    int players = 0;
    if (r == EOS_EResult::EOS_Success) {
      EOS_LobbySearch_GetSearchResultCountOptions co = {EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST};
      uint32_t n = EOS_LobbySearch_GetSearchResultCount(search, &co);
      for (uint32_t i = 0; i < n; i++) {
        EOS_LobbySearch_CopySearchResultByIndexOptions o = {EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST, i};
        EOS_HLobbyDetails h = nullptr;
        if (EOS_LobbySearch_CopySearchResultByIndex(search, &o, &h) != EOS_EResult::EOS_Success) continue;
        EOS_LobbyDetails_CopyInfoOptions io = {EOS_LOBBYDETAILS_COPYINFO_API_LATEST};
        EOS_LobbyDetails_Info *info = nullptr;
        if (EOS_LobbyDetails_CopyInfo(h, &io, &info) != EOS_EResult::EOS_Success) { EOS_LobbyDetails_Release(h); continue; }
        NetRoom room;
        room.id = info->LobbyId;
        room.maxPlayers = (int)info->MaxMembers;
        room.players = (int)(info->MaxMembers - info->AvailableSlots);
        room.name = lobbyAttr(h, "ROOMNAME");
        room.host = lobbyAttr(h, "HOSTNAME");
        if (room.name.empty()) room.name = "room";
        room.open = info->AvailableSlots > 0 && lobbyAttr(h, "STARTED") != "1";
        room.ping = -1;     // P2P: unknown until connected to the host
        EOS_LobbyDetails_Info_Release(info);
        players += room.players;
        details_[room.id] = h;
        list.push_back(room);
      }
    }
    if (search) EOS_LobbySearch_Release(search);
    rooms = list;
    onlineCount = players + (inRoom() ? 0 : 1);
  }

  // ---- inside a room
  void enterRoom(const char *lobbyId, bool owner) {
    roomId = lobbyId;
    roomName = owner ? pendingRoomName_ : roomName;
    state = NET_IN_ROOM;
    lastError.clear();
    lastPingAttr_ = -2;
    myReady_ = false;
    myPing_ = -1;
    matchMap = 0;
    matchLives = 3;
    memberDirty_ = true;
    lobbyStr_.clear();
    lobbyInt_.clear();
    if (owner) {
      lobbyStr_["ROOMNAME"] = pendingRoomName_;
      lobbyStr_["HOSTNAME"] = playerName;
      lobbyInt_["STARTED"] = 0;
      lobbyInt_["LIVES"] = matchLives;
    }
    nextUpdate_ = 0;
    flushUpdates(net_now_ms());
    clearSearch();
    rebuildRoom();
  }

  void rebuildRoom() {
    dirtyMembers_ = false;
    if (roomId.empty()) return;
    EOS_Lobby_CopyLobbyDetailsHandleOptions o = {};
    o.ApiVersion = EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    o.LobbyId = roomId.c_str();
    o.LocalUserId = g_user;
    EOS_HLobbyDetails h = nullptr;
    if (EOS_Lobby_CopyLobbyDetailsHandle(lobby_, &o, &h) != EOS_EResult::EOS_Success) return;
    EOS_LobbyDetails_GetLobbyOwnerOptions oo = {EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST};
    EOS_ProductUserId owner = EOS_LobbyDetails_GetLobbyOwner(h, &oo);
    ownerId_ = uid_str(owner);
    std::string rn = lobbyAttr(h, "ROOMNAME");
    if (!rn.empty()) roomName = rn;
    EOS_LobbyDetails_GetMemberCountOptions co = {EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST};
    uint32_t n = EOS_LobbyDetails_GetMemberCount(h, &co);
    std::vector<NetMember> list;
    for (uint32_t i = 0; i < n; i++) {
      EOS_LobbyDetails_GetMemberByIndexOptions mo = {EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST, i};
      EOS_ProductUserId u = EOS_LobbyDetails_GetMemberByIndex(h, &mo);
      NetMember m;
      m.id = uid_str(u);
      ids_[m.id] = u;
      m.name = memberAttr(h, u, "NAME");
      if (m.name.empty()) m.name = "player";
      m.ready = memberAttr(h, u, "READY") == "1";
      std::string p = memberAttr(h, u, "PING");
      m.ping = p.empty() ? -1 : atoi(p.c_str());
      std::string hero = memberAttr(h, u, "HERO");
      m.hero = hero.empty() ? 0 : atoi(hero.c_str());
      m.host = m.id == ownerId_;
      m.me = u == g_user;
      if (m.me) {                                  // our own values are fresher than the lobby copy
        m.ready = myReady_;
        m.hero = myHero;
        if (!m.host) m.ping = ping;
      }
      list.push_back(m);
    }
    // slot order must be identical on every phone (it decides who is player 1..4 in the match):
    // the host first, then by product user id
    std::sort(list.begin(), list.end(), [](const NetMember &a, const NetMember &b) {
      if (a.host != b.host) return a.host;
      return a.id < b.id;
    });
    members = list;
    {
      std::string mp = lobbyAttr(h, "MAP");
      if (!isHost() || matchMap == 0) matchMap = mp.empty() ? 0 : atoi(mp.c_str());
      std::string lv = lobbyAttr(h, "LIVES");
      if (!isHost() && !lv.empty()) matchLives = atoi(lv.c_str());
    }
    // the owner started the match: everybody (owner included) begins the countdown
    if (state == NET_IN_ROOM && lobbyAttr(h, "STARTED") == "1") {
      std::string seed = lobbyAttr(h, "SEED");
      beginCountdown((uint32_t)atoll(seed.c_str()), net_now_ms());
    }
    EOS_LobbyDetails_Release(h);
  }

  // ---- P2P
  void sendTo(const std::string &uid, uint8_t channel, const std::string &data, bool reliable) {
    auto it = ids_.find(uid);
    if (it == ids_.end()) return;
    EOS_P2P_SendPacketOptions o = {};
    o.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    o.LocalUserId = g_user;
    o.RemoteUserId = it->second;
    o.SocketId = &socket_;
    o.Channel = channel;
    o.DataLengthBytes = (uint32_t)data.size();
    o.Data = data.data();
    o.bAllowDelayedDelivery = EOS_TRUE;
    o.Reliability = reliable ? EOS_EPacketReliability::EOS_PR_ReliableOrdered : EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
    o.bDisableAutoAcceptConnection = EOS_FALSE;
    EOS_P2P_SendPacket(p2p_, &o);
  }

  void sendPing(int64_t now) {
    if (ownerId_.empty() || isHost()) { ping = -1; return; }
    sendTo(ownerId_, CH_PING, "P" + std::to_string(now), false);
  }

  void receivePackets(int64_t now) {
    for (int guard = 0; guard < 64; guard++) {
      EOS_P2P_GetNextReceivedPacketSizeOptions so = {};
      so.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
      so.LocalUserId = g_user;
      uint32_t size = 0;
      if (EOS_P2P_GetNextReceivedPacketSize(p2p_, &so, &size) != EOS_EResult::EOS_Success) return;
      std::vector<char> buf(size ? size : 1);
      EOS_P2P_ReceivePacketOptions ro = {};
      ro.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
      ro.LocalUserId = g_user;
      ro.MaxDataSizeBytes = size;
      EOS_ProductUserId peer = nullptr;
      EOS_P2P_SocketId sock = {};
      uint8_t ch = 0;
      uint32_t got = 0;
      if (EOS_P2P_ReceivePacket(p2p_, &ro, &peer, &sock, &ch, buf.data(), &got) != EOS_EResult::EOS_Success) return;
      std::string data(buf.data(), got), from = uid_str(peer);
      ids_[from] = peer;
      if (data.empty()) continue;
      if (data[0] == 'P') {                        // host: answer pings
        sendTo(from, CH_PING, "Q" + data.substr(1), false);
      } else if (data[0] == 'Q') {                 // member: round trip to the host
        ping = (int)(now - atoll(data.c_str() + 1));
        if (ping < 0) ping = 0;
        for (auto &m : members) if (m.me) m.ping = ping;
        // publish it to the others now and then (lobby updates are rate limited)
        if (std::abs(ping - lastPingAttr_) > 15 || now - lastPingAttrMs_ > 10000) {
          lastPingAttr_ = ping;
          lastPingAttrMs_ = now;
          myPing_ = ping;
          memberDirty_ = true;
        }
      } else if (data[0] == 'G') {                 // game data
        int slot = 0;
        for (size_t i = 0; i < members.size(); i++) if (members[i].id == from) slot = (int)i + 1;
        inbox.push_back({slot, data.substr(1), from});
        if (inbox.size() > NET_INBOX_MAX) inbox.pop_front();
      }
    }
  }

 public:
  NetEos() { alive_set().insert(this); }
};

}  // namespace

NetBackend *net_create_eos() { return new NetEos(); }

#endif  // MH_WITH_EOS
