
#include "RprTools.h"
#include <vector>
#include <string>
#include <cstring>

//return true if same string (case insensitive)
//not case sensitive
bool strcmp_caseInsensitive(const char* strA, const char* strB )
{
	for(int i=0; ;i++)
	{
		char chara = strA[i];
		char charb = strB[i];

		if ( chara >= 'A' && chara <= 'Z' )
		{
			chara = chara-'A'+'a';
		}
		if ( charb >= 'A' && charb <= 'Z' )
		{
			charb = charb-'A'+'a';
		}

		if ( chara != charb )
		{
			return false;
		}

		if ( chara == '\0' ) 
		{
			break;
		}
	}

	return true;
}

//return true if strA contains strB
//not case sensitive
bool strstr_caseInsensitive(const char* strA, const char* strB )
{
	std::string strA_lowercase;
	for(int i=0; ;i++) 
	{  
		char newchar = strA[i];
		if ( newchar == '\0' ) { break; }
		if ( newchar >= 'A' && newchar <= 'Z' )
		{
			newchar = newchar-'A'+'a';
		}
		strA_lowercase.push_back(newchar);
	}

	std::string strB_lowercase;
	for(int i=0; ;i++) 
	{  
		char newchar = strB[i];
		if ( newchar == '\0' ) { break; }
		if ( newchar >= 'A' && newchar <= 'Z' )
		{
			newchar = newchar-'A'+'a';
		}
		strB_lowercase.push_back(newchar);
	}


	if (strA_lowercase.find(strB_lowercase) != std::string::npos) 
	{
		return true;
	}
	return false;
}

bool IsDeviceNameWhitelisted(const char* deviceName, RPR_TOOLS_OS os)
{
	//
	//this is the list of compatible devices known by the Radeon ProRender team.
	//no need of case sensitivity
	std::vector<std::string> listOfKnownCompatibleDevices_exact; // exact names, example : "AMD Radeon (TM) Pro WX 4100 Graphics"
	std::vector<std::string> listOfKnownCompatibleDevices_partial; // partial names, example : "WX 4100"

	//AMD
	listOfKnownCompatibleDevices_partial.push_back("FirePro W600");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W2100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W4100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W4300");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W5000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W5100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W7000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W7100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W8000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W8100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W9000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro W9100");
	//listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) R9 Fury Series");
	listOfKnownCompatibleDevices_exact.push_back("Radeon (TM) Pro Duo");
	listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) Pro Duo");
	//listOfKnownCompatibleDevices_exact.push_back("Radeon (TM) RX 480 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) RX 480 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("Radeon (TM) Pro WX 7100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) Pro WX 7100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("Radeon (TM) Pro WX 5100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) Pro WX 5100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("Radeon (TM) Pro WX 4100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("AMD Radeon (TM) Pro WX 4100 Graphics");
	//listOfKnownCompatibleDevices_exact.push_back("Radeon Pro WX4100 Graphics");

	listOfKnownCompatibleDevices_partial.push_back("FirePro S4000X");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S7000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S7100X");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S7150");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S7150x2");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9000");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9050");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9100");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9150");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9170");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S9300 X2");
	listOfKnownCompatibleDevices_partial.push_back("FirePro S10000");

	if ( os != RPRTOS_MACOS )
	{
		// NVIDIA
		listOfKnownCompatibleDevices_exact.push_back("Nvidia GTX 680M");
		listOfKnownCompatibleDevices_exact.push_back("quadro m6000");
		listOfKnownCompatibleDevices_exact.push_back("quadro m5000");
		listOfKnownCompatibleDevices_exact.push_back("quadro m4000");
		listOfKnownCompatibleDevices_exact.push_back("quadro k5200");
		listOfKnownCompatibleDevices_exact.push_back("quadro k4200");
	}

	//
	// list of partial names :
	//
	listOfKnownCompatibleDevices_partial.push_back("Radeon Pro WX"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon (TM) Pro WX"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon R9"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon (TM) R9"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon RX"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon (TM) RX"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon Vega Frontier Edition"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon Frontier"); 
	listOfKnownCompatibleDevices_partial.push_back("Radeon(TM) Pro Duo");

	// partial names - WxxxM
	listOfKnownCompatibleDevices_partial.push_back("W4170M"); 
	listOfKnownCompatibleDevices_partial.push_back("W4190M"); 
	listOfKnownCompatibleDevices_partial.push_back("W5130M"); 
	listOfKnownCompatibleDevices_partial.push_back("W5170M"); 
	listOfKnownCompatibleDevices_partial.push_back("W6150M"); 
	listOfKnownCompatibleDevices_partial.push_back("W6170M"); 
	listOfKnownCompatibleDevices_partial.push_back("W7170M"); 




	for (std::vector<std::string>::iterator iCompatibleDevices = listOfKnownCompatibleDevices_exact.begin() ; iCompatibleDevices != listOfKnownCompatibleDevices_exact.end(); ++iCompatibleDevices)
	{
		if ( strcmp_caseInsensitive(  deviceName, (*iCompatibleDevices).c_str()  )   )
		{
			//compatible device found
			return true;
		}
	}

	for (std::vector<std::string>::iterator iCompatibleDevices = listOfKnownCompatibleDevices_partial.begin() ; iCompatibleDevices != listOfKnownCompatibleDevices_partial.end(); ++iCompatibleDevices)
	{
		if ( strstr_caseInsensitive(  deviceName, (*iCompatibleDevices).c_str()  )   )
		{
			//compatible device found
			return true;
		}
	}

	return false;
}

#ifndef RADEONPRORENDERTOOLS_DONTUSERPR
#include <Windows.h>
RPR_TOOLS_COMPATIBILITY rprIsDeviceCompatible(const rpr_char* rendererDLL, RPR_TOOLS_DEVICE device, rpr_char const * cache_path, bool doWhiteListTest, RPR_TOOLS_OS os)
{
    rpr_int status = RPR_SUCCESS;

    rpr_context temporaryContext = 0;

	try 
	{

		//step 1:
		//we try to create a context with this device :
		//the frCreateContext we check that the GPU is OpenCL compatible, and exist.
        rpr_creation_flags flags = 0;
        rpr_context_info contextInfo = 0;
	
		{
            rpr_int tahoePluginID = rprRegisterPlugin(rendererDLL);
			if ( tahoePluginID == -1 ) { throw  RPRTC_INCOMPATIBLE_UNKNOWN; }
            rpr_int plugins[] = { tahoePluginID};
			size_t pluginCount = sizeof(plugins) / sizeof(plugins[0]);
				 if ( device == RPRTD_GPU0 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU0; contextInfo = RPR_CONTEXT_GPU0_NAME; }
			else if ( device == RPRTD_GPU1 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU1; contextInfo = RPR_CONTEXT_GPU1_NAME;}
			else if ( device == RPRTD_GPU2 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU2; contextInfo = RPR_CONTEXT_GPU2_NAME;}
			else if ( device == RPRTD_GPU3 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU3; contextInfo = RPR_CONTEXT_GPU3_NAME;}
			else if ( device == RPRTD_GPU4 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU4; contextInfo = RPR_CONTEXT_GPU4_NAME;}
			else if ( device == RPRTD_GPU5 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU5; contextInfo = RPR_CONTEXT_GPU5_NAME;}
			else if ( device == RPRTD_GPU6 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU6; contextInfo = RPR_CONTEXT_GPU6_NAME;}
			else if ( device == RPRTD_GPU7 ) { flags = RPR_CREATION_FLAGS_ENABLE_GPU7; contextInfo = RPR_CONTEXT_GPU7_NAME;}
			else if ( device == RPRTD_CPU )  { flags = RPR_CREATION_FLAGS_ENABLE_CPU;  contextInfo = RPR_CONTEXT_CPU_NAME;}
			else { throw  RPRTC_INCOMPATIBLE_UNKNOWN; }
			status = rprCreateContext(RPR_API_VERSION, plugins, pluginCount, flags, NULL, cache_path, &temporaryContext);
			if ( status != RPR_SUCCESS )
			{
				if ( status == RPR_ERROR_UNSUPPORTED )
				{
					throw  RPRTC_INCOMPATIBLE_CONTEXT_UNSUPPORTED;
				}
				else
				{
					throw  RPRTC_INCOMPATIBLE_CONTEXT_ERROR;
				}
			}
		}
	
	
		//step 2:
		//we check that the device is in the list compatible devices.
		if ( doWhiteListTest )
		{
			size_t size = 0;
			status = rprContextGetInfo(temporaryContext,contextInfo,0,0,&size);
			if ( status != RPR_SUCCESS ) { throw  RPRTC_INCOMPATIBLE_UNKNOWN; }

			char* deviceName = new char[size];
			status = rprContextGetInfo(temporaryContext,contextInfo,size,deviceName,0);
			if ( status != RPR_SUCCESS ) { throw  RPRTC_INCOMPATIBLE_UNKNOWN; }
		
			if ( !IsDeviceNameWhitelisted(deviceName,os) )
			{
				throw RPRTC_INCOMPATIBLE_UNCERTIFIED;
			}

			delete[] deviceName; deviceName=nullptr;
		}




	}
	catch(RPR_TOOLS_COMPATIBILITY i )
	{
		if ( temporaryContext )
		{
			status = rprObjectDelete(temporaryContext); temporaryContext = NULL;
			if ( status != RPR_SUCCESS ) { return RPRTC_INCOMPATIBLE_UNKNOWN; }
		}
        return i;
  }

	if ( temporaryContext )
	{
		status = rprObjectDelete(temporaryContext); temporaryContext = NULL;
		if (status != RPR_SUCCESS ) { return RPRTC_INCOMPATIBLE_UNKNOWN; }
	}

	return RPRTC_COMPATIBLE;
}



void rprAreDevicesCompatible(const rpr_char* rendererDLL, rpr_char const * cache_path, bool doWhiteListTest, rpr_creation_flags devicesUsed,  rpr_creation_flags* devicesCompatibleOut, RPR_TOOLS_OS os)
{
	*devicesCompatibleOut = devicesUsed;

	rpr_int compatibility = RPR_ERROR_INVALID_PARAMETER;
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU0 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU0,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU0; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU1 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU1,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU1; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU2 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU2,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU2; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU3 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU3,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU3; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU4 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU4,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU4; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU5 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU5,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU5; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU6 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU6,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU6; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_GPU7 ) { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_GPU7,cache_path,doWhiteListTest,os) != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_GPU7; } }
	if ( devicesUsed & RPR_CREATION_FLAGS_ENABLE_CPU )  { if ( rprIsDeviceCompatible(rendererDLL,RPRTD_CPU,cache_path,doWhiteListTest,os)  != RPRTC_COMPATIBLE ) { *devicesCompatibleOut &= ~RPR_CREATION_FLAGS_ENABLE_CPU; } }

	return;
}


#endif






