@ECHO OFF

SET WIDTH=800
SET HEIGHT=600

SET OUTPUT_FOLDER=%1
if "%OUTPUT_FOLDER%"=="" SET OUTPUT_FOLDER=./
SET DATA_FOLDER=%2
SET SCENE=%3
SET LIGHT=%4
SET MIN=%5
SET MAX=%6

if "%MIN%"=="" SET MIN="1"
if "%MAX%"=="" SET MAX="-1"

IF "%SCENE%"=="cornellbox"      CALL :render_aovs ../Resources/CornellBox    orig.objm          %DATA_FOLDER%/CornellBox/cam.xml        %DATA_FOLDER%/CornellBox/%LIGHT%.xml     %OUTPUT_FOLDER%/CornellBox 4096
IF "%SCENE%"=="sponza"          CALL :render_aovs ../Resources/sponza        sponza.obj         %DATA_FOLDER%/sponza/cam.xml            %DATA_FOLDER%/sponza/%LIGHT%.xml         %OUTPUT_FOLDER%/Sponza 4096
IF "%SCENE%"=="salle_de_bain"   CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj  %DATA_FOLDER%/salle_de_bain/cam.xml     %DATA_FOLDER%/salle_de_bain/%LIGHT%.xml  %OUTPUT_FOLDER%/salle_de_bain 16000
IF "%SCENE%"=="san-miguel"      CALL :render_aovs ../Resources/san-miguel    san-miguel.obj     %DATA_FOLDER%/san-miguel/cam.xml        %DATA_FOLDER%/san-miguel/%LIGHT%.xml     %OUTPUT_FOLDER%/san-miguel 4096 
IF "%SCENE%"=="cloister"        CALL :render_aovs ../Resources/cloister      cloister.obj       %DATA_FOLDER%/cloister/cam.xml          %DATA_FOLDER%/cloister/%LIGHT%.xml       %OUTPUT_FOLDER%/cloister 4096
IF "%SCENE%"=="kitchen"         CALL :render_aovs ../Resources/kitchen       kitchen_mats2.obj  %DATA_FOLDER%/kitchen/cam.xml           %DATA_FOLDER%/kitchen/%LIGHT%.xml        %OUTPUT_FOLDER%/kitchen 100

EXIT /B %ERRORLEVEL% 

rem args:
rem folder with scene
rem scene
rem camera log file
rem light set file
rem folder to save results
rem max number of samples
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
SET MAX_SAMPLES=%6

rem name directory same as light set
FOR %%i IN ("%LIGHT_SET%") DO SET SUBDIR_LIGHT=%OUT%/%%~ni
IF NOT EXIST "%SUBDIR_LIGHT%" mkdir "%SUBDIR_LIGHT%"
echo %SUBDIR_LIGHT%
rem iterate through different camera settings
rem and create 
echo "../build/bin/Release/BaikalStandalone.exe" -w %WIDTH% -h %HEIGHT% -p  %SCENE_FOLDER% -f %SCENE% -light_set %LIGHT_SET% -save_aov -camera_set %CAMERA_LOG_FILE% -output_aov %SUBDIR_LIGHT% -aov_samples %MAX_SAMPLES% -camera_set_min %MIN% -camera_set_max %MAX%
"../build/bin/Release/BaikalStandalone.exe" -w %WIDTH% -h %HEIGHT% -p  %SCENE_FOLDER% -f %SCENE% -light_set %LIGHT_SET% -save_aov -camera_set %CAMERA_LOG_FILE% -output_aov %SUBDIR_LIGHT% -aov_samples %MAX_SAMPLES% -camera_set_min %MIN% -camera_set_max %MAX% > NUL

rem 
EXIT /B 0