@echo off
rem Starts a small local web server and opens MultiHero in the browser (needs Python 3).
cd /d "%~dp0"
start "" http://localhost:8080/
python -m http.server 8080 || py -m http.server 8080
pause
