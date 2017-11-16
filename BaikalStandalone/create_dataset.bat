@ECHO OFF

SET WIDTH=800
SET HEIGHT=800

SET OUTPUT_FOLDER=%1
if [OUTPUT_FOLDER]==[] SET OUTPUT_FOLDER=./


CALL :render_aovs ../Resources/CornellBox orig.objm camera.log ../../light1.ls %OUTPUT_FOLDER%/CornellBox
CALL :render_aovs ../Resources/CornellBox orig.objm camera.log ../../light2.ls %OUTPUT_FOLDER%/CornellBox
CALL :render_aovs ../Resources/sponza sponza.obj camera.log ../../light1.ls %OUTPUT_FOLDER%/Sponza
CALL :render_aovs ../Resources/sponza sponza.obj camera.log ../../light2.ls %OUTPUT_FOLDER%/Sponza
pause
EXIT /B %ERRORLEVEL% 


:render_aovs
rem folder containing scene
SET SCENE_FOLDER=%~1
rem scene filename
SET SCENE=%~2
rem file with cameras
SET CAMERA_LOG_FILE=%~3
rem light description
SET LIGHT_SET=%~4
rem folder to store data
SET OUT=%~5

rem name directory same as light set
FOR %%i IN ("%LIGHT_SET%") DO SET SUBDIR_LIGHT=%OUT%/%%~ni

rem iterate through different camera settings
rem output for each camera stored in separated folder
setlocal EnableDelayedExpansion
SET i=0
for /f "tokens=*" %%a in (%CAMERA_LOG_FILE%) do (
    SET /A i+=1
    SET SUBDIR_CAM=!SUBDIR_LIGHT!/!i!
    IF NOT EXIST "!SUBDIR_CAM!" mkdir "!SUBDIR_CAM!"
    "../Bin/Release/x64/BaikalStandalone64.exe" -w !WIDTH! -h !HEIGHT! -e ../Resources/Textures/Harbor_3_Free_Ref.hdr -p  %SCENE_FOLDER% -f %SCENE% -light_set !LIGHT_SET! -save_aov %%a -output_aov !SUBDIR_CAM! > NUL
)
rem 
EXIT /B 0