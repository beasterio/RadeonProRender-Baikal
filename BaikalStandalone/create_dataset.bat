@ECHO OFF

SET WIDTH=800
SET HEIGHT=600

SET OUTPUT_FOLDER=%1
if "%OUTPUT_FOLDER%"=="" SET OUTPUT_FOLDER=./

rem CornellBox
CALL :render_aovs ../Resources/CornellBox orig.objm ../Resources/data/CornellBox/cornellbox_cam.log ../Resources/data/CornellBox/morning.ls %OUTPUT_FOLDER%/CornellBox 2048
CALL :render_aovs ../Resources/CornellBox orig.objm ../Resources/data/CornellBox/cornellbox_cam.log ../Resources/data/CornellBox/day.ls %OUTPUT_FOLDER%/CornellBox 2048
CALL :render_aovs ../Resources/CornellBox orig.objm ../Resources/data/CornellBox/cornellbox_cam.log ../Resources/data/CornellBox/evening.ls %OUTPUT_FOLDER%/CornellBox 2048
CALL :render_aovs ../Resources/CornellBox orig.objm ../Resources/data/CornellBox/cornellbox_cam.log ../Resources/data/CornellBox/night.ls %OUTPUT_FOLDER%/CornellBox 2048
CALL :render_aovs ../Resources/CornellBox orig.objm ../Resources/data/CornellBox/cornellbox_cam.log ../Resources/data/CornellBox/cloudy.ls %OUTPUT_FOLDER%/CornellBox 2048
 
rem sponza
CALL :render_aovs ../Resources/sponza sponza.obj ../Resources/data/sponza/sponza_cam.log ../Resources/data/sponza/morning.ls %OUTPUT_FOLDER%/Sponza 2048
CALL :render_aovs ../Resources/sponza sponza.obj ../Resources/data/sponza/sponza_cam.log ../Resources/data/sponza/day.ls %OUTPUT_FOLDER%/Sponza 2048
CALL :render_aovs ../Resources/sponza sponza.obj ../Resources/data/sponza/sponza_cam.log ../Resources/data/sponza/evening.ls %OUTPUT_FOLDER%/Sponza 2048
CALL :render_aovs ../Resources/sponza sponza.obj ../Resources/data/sponza/sponza_cam.log ../Resources/data/sponza/night.ls %OUTPUT_FOLDER%/Sponza 2048
CALL :render_aovs ../Resources/sponza sponza.obj ../Resources/data/sponza/sponza_cam.log ../Resources/data/sponza/cloudy.ls %OUTPUT_FOLDER%/Sponza 2048
 

rem salle_de_bain
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj ../Resources/data/salle_de_bain/salle_de_bain_cam.log ../Resources/data/salle_de_bain/morning.ls %OUTPUT_FOLDER%/salle_de_bain 2048
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj ../Resources/data/salle_de_bain/salle_de_bain_cam.log ../Resources/data/salle_de_bain/day.ls %OUTPUT_FOLDER%/salle_de_bain 2048
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj ../Resources/data/salle_de_bain/salle_de_bain_cam.log ../Resources/data/salle_de_bain/evening.ls %OUTPUT_FOLDER%/salle_de_bain 2048
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj ../Resources/data/salle_de_bain/salle_de_bain_cam.log ../Resources/data/salle_de_bain/night.ls %OUTPUT_FOLDER%/salle_de_bain 2048
CALL :render_aovs ../Resources/salle_de_bain salle_de_bain.obj ../Resources/data/salle_de_bain/salle_de_bain_cam.log ../Resources/data/salle_de_bain/cloudy.ls %OUTPUT_FOLDER%/salle_de_bain 2048

rem san-miguel
CALL :render_aovs ../Resources/san-miguel san-miguel.obj ../Resources/data/san-miguel/san-miguel_cam.log ../Resources/data/san-miguel/morning.ls %OUTPUT_FOLDER%/san-miguel 2048 
CALL :render_aovs ../Resources/san-miguel san-miguel.obj ../Resources/data/san-miguel/san-miguel_cam.log ../Resources/data/san-miguel/day.ls %OUTPUT_FOLDER%/san-miguel 2048
CALL :render_aovs ../Resources/san-miguel san-miguel.obj ../Resources/data/san-miguel/san-miguel_cam.log ../Resources/data/san-miguel/evening.ls %OUTPUT_FOLDER%/san-miguel 2048
CALL :render_aovs ../Resources/san-miguel san-miguel.obj ../Resources/data/san-miguel/san-miguel_cam.log ../Resources/data/san-miguel/night.ls %OUTPUT_FOLDER%/san-miguel 2048
CALL :render_aovs ../Resources/san-miguel san-miguel.obj ../Resources/data/san-miguel/san-miguel_cam.log ../Resources/data/san-miguel/cloudy.ls %OUTPUT_FOLDER%/san-miguel 2048

rem cloister
CALL :render_aovs ../Resources/cloister cloister.obj ../Resources/data/cloister/cloister_cam.log ../Resources/data/cloister/morning.ls %OUTPUT_FOLDER%/cloister 2048
CALL :render_aovs ../Resources/cloister cloister.obj ../Resources/data/cloister/cloister_cam.log ../Resources/data/cloister/day.ls %OUTPUT_FOLDER%/cloister 2048
CALL :render_aovs ../Resources/cloister cloister.obj ../Resources/data/cloister/cloister_cam.log ../Resources/data/cloister/evening.ls %OUTPUT_FOLDER%/cloister 2048
CALL :render_aovs ../Resources/cloister cloister.obj ../Resources/data/cloister/cloister_cam.log ../Resources/data/cloister/night.ls %OUTPUT_FOLDER%/cloister 2048
CALL :render_aovs ../Resources/cloister cloister.obj ../Resources/data/cloister/cloister_cam.log ../Resources/data/cloister/cloudy.ls %OUTPUT_FOLDER%/cloister 2048

rem kitchen
CALL :render_aovs ../Resources/kitchen kitchen_mats2.obj ../Resources/data/kitchen/kitchen_cam.log ../Resources/data/kitchen/morning.ls %OUTPUT_FOLDER%/kitchen 2048
CALL :render_aovs ../Resources/kitchen kitchen_mats2.obj ../Resources/data/kitchen/kitchen_cam.log ../Resources/data/kitchen/day.ls %OUTPUT_FOLDER%/kitchen 2048
CALL :render_aovs ../Resources/kitchen kitchen_mats2.obj ../Resources/data/kitchen/kitchen_cam.log ../Resources/data/kitchen/evening.ls %OUTPUT_FOLDER%/kitchen 2048
CALL :render_aovs ../Resources/kitchen kitchen_mats2.obj ../Resources/data/kitchen/kitchen_cam.log ../Resources/data/kitchen/night.ls %OUTPUT_FOLDER%/kitchen 2048
CALL :render_aovs ../Resources/kitchen kitchen_mats2.obj ../Resources/data/kitchen/kitchen_cam.log ../Resources/data/kitchen/cloudy.ls %OUTPUT_FOLDER%/kitchen 2048

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
"../Bin/Release/x64/BaikalStandalone64.exe" -w %WIDTH% -h %HEIGHT% -p  %SCENE_FOLDER% -f %SCENE% -light_set %LIGHT_SET% -save_aov -read_camera_log %CAMERA_LOG_FILE% -output_aov %SUBDIR_LIGHT% -aov_samples %MAX_SAMPLES% > NUL

rem 
EXIT /B 0