@ECHO OFF

SET WIDTH=800
SET HEIGHT=600

SET OUTPUT_FOLDER=%1
if "%OUTPUT_FOLDER%"=="" SET OUTPUT_FOLDER=./

CALL :render_aovs ../Resources/CornellBox orig.objm cornellbox_cam.log ../../light2.ls %OUTPUT_FOLDER%/CornellBox
CALL :render_aovs ../Resources/sponza sponza.obj sponza_cam.log ../../light2.ls %OUTPUT_FOLDER%/Sponza
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj salle_de_bain_cam.log ../../light2.ls %OUTPUT_FOLDER%/salle_de_bain
CALL :render_aovs ../Resources/san-miguel san-miguel.obj san-miguel_cam.log ../../light2.ls %OUTPUT_FOLDER%/san-miguel
CALL :render_aovs ../Resources/cloister cloister.obj cloister_cam.log ../../light2.ls %OUTPUT_FOLDER%/cloister
CALL :render_aovs ../Resources/kitchen kitchen.obj kitchen_cam.log ../../light2.ls %OUTPUT_FOLDER%/kitchen
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
    "../Bin/Release/x64/BaikalStandalone64.exe" -w !WIDTH! -h !HEIGHT! -p  %SCENE_FOLDER% -f %SCENE% -light_set !LIGHT_SET! -save_aov %%a -output_aov !SUBDIR_CAM! > NUL
)
rem 
EXIT /B 0