# create_dataset.bat usage
Working directory - ./BaikalStandalone

Script expecting next inputs:
1. Output folder. All rendered images will be stored here. Folder sorted by scene name and light set.
2. Data folder with *.xml configuration files. See _./Resources/data/_.
3. Scene name. Should be one of these values: cornellbox, sponza, salle_de_bain, san-miguel, cloister, kitchen.
4. Light name. Should be one of these values: cloudy, day, evening, morning, night.
5. Starting index of camera list. default value 1
6. End index of camera list. Default value -1 (till the end of the list).

Example of usage:
```
cd BaikalStandalone
create_dataset.bat ../../Output/ ../Resources/data/ cornellbox night 1 10
```
This will render images to ../../Output/cornellbox/night folder for first 10 cameras from ../Resources/data/CornellBox/cam.xml and ../Resources/data/CornellBox/night.xml light.


# xml files format
## cam.xml

root element named **cam_list** and contains many **camera** elements with attributes:
- camera position(cpx, cpy, cpz).
- camera lookat(tpx, tpy, tpz).
- camera aperture, focus distance and focal length named accordingly.

cam.xml can be generated/updated using standalone app:
- run app
- move camera to desired position
- press **C** key to add current camera to the file.

Standalone input **-camera_out_folder some_folder** will specify folder with cam.xml. If file already exist new camera position added to the end of list. Default file: __./BaikalStandalone/cam.xml__

## light xml

root element named **light_list** and contains many **light** elements with attributes for all lights:
- type - attribute type. Can be ibl, spot, direct, point.
- light position(posx, posy, posz)
- light direction(dirx, diry, dirz)
- light radiance(radx, rady, radz)

ibl only attributes:
- tex texture filename.
- mul - ibl multiplier.

spot light attributes:
- cone shape(csx, csy).

Current light setup can be saved to light.xml by pressing **L** key.
There is no tool to change light in app runtime, so light set can be changed only from code or manually in xml file.  
## samples.xml
contain samples number for each need to save AOVs. Can be unordered. Created and updated manually.
