@ECHO OFF

rem CALL :render_aovs ../Resources/sponza sponza.obj
CALL :render_aovs ../Resources/CornellBox orig.objm camera.log
pause
EXIT /B %ERRORLEVEL% 


:render_aovs
SET SCENE_FOLDER=%~1
SET SCENE=%~2
SET CAMERA_LOG_FILE=%~3

for /f "tokens=*" %%a in (%CAMERA_LOG_FILE%) do (
    "../Bin/Release/x64/BaikalStandalone64.exe" -e ../Resources/Textures/Harbor_3_Free_Ref.hdr -p  %SCENE_FOLDER% -f %SCENE% -save_aov %%a
)
rem 
EXIT /B 0