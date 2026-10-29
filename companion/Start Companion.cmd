@echo off
cd /d "%~dp0"
python companion.py
if errorlevel 1 pause
