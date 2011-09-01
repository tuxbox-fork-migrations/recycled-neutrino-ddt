//=============================================================================
// NHTTPD
// Neutrino ControlAPI
//=============================================================================
#include <config.h>
// C
#include <stdio.h>
#include <stdint.h>
#include <cctype>

// C++
#include <string>
#include <fstream>
#include <map>
// system
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <unistd.h>
#include <dirent.h>
// tuxbox
#include <global.h>
#include <neutrinoMessages.h>
#include <zapit/client/zapittools.h>
#include <zapit/channel.h>
#include <zapit/bouquets.h>
#include <configfile.h>
#include <system/configure_network.h>
#include <cs_api.h>
#include <global.h>
// yhttpd
#include "yhttpd.h"
#include "ytypes_globals.h"
#include "ylogging.h"
#include "helper.h"
// nhttpd
#include "neutrinoapi.h"
#include "controlapi.h"

bool sectionsd_getEPGidShort(event_id_t epgID, CShortEPGData * epgdata);
bool sectionsd_getEPGid(const event_id_t epgID, const time_t startzeit, CEPGData * epgdata);
void sectionsd_getEventsServiceKey(t_channel_id serviceUniqueKey, CChannelEventList &eList, char search = 0, std::string search_text = "");
void sectionsd_getCurrentNextServiceKey(t_channel_id uniqueServiceKey, CSectionsdClient::responseGetCurrentNextInfoChannelID& current_next );
bool sectionsd_getLinkageDescriptorsUniqueKey(const event_id_t uniqueKey, CSectionsdClient::LinkageDescriptorList& descriptors);
bool sectionsd_getComponentTagsUniqueKey(const event_id_t uniqueKey, CSectionsdClient::ComponentTagList& tags);
extern CBouquetManager *g_bouquetManager;
extern t_channel_id live_channel_id;
#define EVENTDEV "/dev/input/input0"

//-----------------------------------------------------------------------------
enum {	// not defined in input.h but used like that, at least in 2.4.22
	KEY_RELEASED = 0,
	KEY_PRESSED,
	KEY_AUTOREPEAT
};

//=============================================================================
// Initialization of static variables
//=============================================================================
std::string CControlAPI::PLUGIN_DIRS[PLUGIN_DIR_COUNT];

//=============================================================================
// constructor und destructor
//=============================================================================
CControlAPI::CControlAPI(CNeutrinoAPI *_NeutrinoAPI)
{
	NeutrinoAPI = _NeutrinoAPI;
}
//-----------------------------------------------------------------------------
void CControlAPI::init(CyhookHandler *hh)
{
	if(PLUGIN_DIRS[0] == "")
	{	// given in nhttpd.conf
		PLUGIN_DIRS[0]=PLUGIN_DIRS[1]=hh->WebserverConfigList["WebsiteMain.override_directory"];
		PLUGIN_DIRS[1].append("/scripts");
		PLUGIN_DIRS[2]=PLUGIN_DIRS[3]=hh->WebserverConfigList["WebsiteMain.directory"];
		PLUGIN_DIRS[3].append("/scripts");
		PLUGIN_DIRS[4]="/var/tuxbox/plugins";
		PLUGIN_DIRS[5]=PLUGINDIR;
		PLUGIN_DIRS[6]="/mnt/plugins";
	}
}

//=============================================================================
// Hooks!
//=============================================================================
//-----------------------------------------------------------------------------
THandleStatus CControlAPI::Hook_PrepareResponse(CyhookHandler *hh)
{
	init(hh);

	if(hh->UrlData["path"] == "/control/"
			|| hh->UrlData["path"] == "/cgi-bin/"
			|| hh->UrlData["path"] == "/fb/"
	  )
		return HANDLED_READY;
	else
		return HANDLED_NONE;
}
//-----------------------------------------------------------------------------
// HOOK: response_hook Handler
// This is the main dispatcher for this module
//-----------------------------------------------------------------------------
THandleStatus CControlAPI::Hook_SendResponse(CyhookHandler *hh)
{
	hh->status = HANDLED_NONE;

//	log_level_printfX(4,"CControlAPI hook start url:%s\n",hh->UrlData["url"].c_str());
	init(hh);

	if(hh->UrlData["path"] == "/control/"
			|| hh->UrlData["path"] == "/cgi-bin/")
		Execute(hh);
	if(hh->UrlData["path"] == "/fb/")		// fb-compatibility for timer-calls
		compatibility_Timer(hh);
//	log_level_printfX(4,"CControlAPI hook ende status:%d\n",(int)hh->status);
//	log_level_printfX(5,"CControlAPI hook result:%s\n",hh->yresult.c_str());

	return hh->status;
}

//=============================================================================
//-------------------------------------------------------------------------
// timer compatibility
// do add/modify/remove and Return (redirect) Timerlist
//-------------------------------------------------------------------------
void CControlAPI::compatibility_Timer(CyhookHandler *hh)
{
	log_level_printf(4,"CControlAPI Compatibility Timer Start url:%s\n",hh->UrlData["url"].c_str());
	if(NeutrinoAPI->Timerd->isTimerdAvailable() && hh->ParamList.size() > 0)
	{
		if(hh->ParamList["action"] == "remove")
		{
			unsigned removeId = atoi(hh->ParamList["id"].c_str());
			NeutrinoAPI->Timerd->removeTimerEvent(removeId);
		}
		else if(hh->ParamList["action"] == "modify")
			doModifyTimer(hh);
		else if(hh->ParamList["action"] == "new")
			doNewTimer(hh);
	}
	hh->SendRedirect("/Y_Timer_List.yhtm");
}

//=============================================================================
// Main Dispatcher / Call definitions
//=============================================================================
const CControlAPI::TyCgiCall CControlAPI::yCgiCallList[]=
{
	// channel & bouquet & epg & zapping handling
	{"getservicesxml", 	&CControlAPI::GetServicesxmlCGI,""},
	{"getbouquetsxml", 	&CControlAPI::GetBouquetsxmlCGI,""},
	{"channellist", 	&CControlAPI::ChannellistCGI,	"text/plain"},
	{"getbouquet", 		&CControlAPI::GetBouquetCGI,	"+xml"},
	{"getbouquets", 	&CControlAPI::GetBouquetsCGI,	"+xml"},
	{"getmode", 		&CControlAPI::GetModeCGI,		"text/plain"},
	{"setmode", 		&CControlAPI::SetModeCGI,		"text/plain"},
	{"epg", 			&CControlAPI::EpgCGI,			""},
	{"zapto", 			&CControlAPI::ZaptoCGI,			"text/plain"},
	{"getonidsid", 		&CControlAPI::GetChannel_IDCGI,	"text/plain"},
	// boxcontrol - system
	{"standby", 		&CControlAPI::StandbyCGI,		"text/plain"},
	{"shutdown", 		&CControlAPI::ShutdownCGI,		"text/plain"},
	{"reboot", 			&CControlAPI::RebootCGI,		"text/plain"},
	{"getdate", 		&CControlAPI::GetDateCGI,		"text/plain"},
	{"gettime", 		&CControlAPI::GetTimeCGI,		"text/plain"},
	{"info", 			&CControlAPI::InfoCGI,			"text/plain"},
	{"version", 		&CControlAPI::VersionCGI,		""},
	{"reloadsetup", 	&CControlAPI::ReloadNutrinoSetupfCGI,		""},
	// boxcontrol - devices
	{"volume", 			&CControlAPI::VolumeCGI,		"text/plain"},
	{"lcd", 			&CControlAPI::LCDAction,		"text/plain"},
	{"system", 			&CControlAPI::SystemCGI,		"text/plain"},
	{"message", 		&CControlAPI::MessageCGI,		"text/plain"},
	{"rc", 				&CControlAPI::RCCGI,			"text/plain"},
	{"rcem", 			&CControlAPI::RCEmCGI,			"text/plain"},
	// Start skripts, plugins
	{"startplugin", 	&CControlAPI::StartPluginCGI,	"text/plain"},
	{"exec", 			&CControlAPI::ExecCGI,			"+xml"},
	{"yweb", 			&CControlAPI::YWebCGI,			"text/plain"},
	// video & Audio handling
	{"aspectratio", 	&CControlAPI::AspectRatioCGI,	"text/plain"},
	{"videoformat", 	&CControlAPI::VideoFormatCGI,	"text/plain"},
	{"videooutput", 	&CControlAPI::VideoOutputCGI,	"text/plain"},
	{"vcroutput", 		&CControlAPI::VCROutputCGI,		"text/plain"},
	{"scartmode", 		&CControlAPI::ScartModeCGI,		"text/plain"},
	{"audio", 			&CControlAPI::AudioCGI,			"text/plain"},
	{"crypt", 			&CControlAPI::CryptCGI,			"text/plain"},
	// timer
	{"timer", 			&CControlAPI::TimerCGI,			"text/plain"},
	// bouquet editing
	{"setbouquet", 		&CControlAPI::setBouquetCGI,	"text/plain"},
	{"savebouquet",		&CControlAPI::saveBouquetCGI,	"text/plain"},
	{"movebouquet",		&CControlAPI::moveBouquetCGI,	"text/plain"},
	{"deletebouquet",	&CControlAPI::deleteBouquetCGI,	"text/plain"},
	{"addbouquet",		&CControlAPI::addBouquetCGI,	"text/plain"},
	{"renamebouquet",	&CControlAPI::renameBouquetCGI,	"text/plain"},
	{"changebouquet",	&CControlAPI::changeBouquetCGI,	"text/plain"},
	{"updatebouquet",	&CControlAPI::updateBouquetCGI,	"text/plain"},
	// utils
	{"build_live_url",	&CControlAPI::build_live_url,	""},
	{"get_logo",		&CControlAPI::logoCGI,	"text/plain"},
	// settings
	{"config",			&CControlAPI::ConfigCGI,	"text/plain"},
	// filehandling
	{"file",			&CControlAPI::FileCGI,	"+xml"},


};
//-----------------------------------------------------------------------------
// Main Dispatcher
//-----------------------------------------------------------------------------
void CControlAPI::Execute(CyhookHandler *hh)
{
	int index = -1;
	std::string yresult;
	std::string filename = hh->UrlData["filename"];

	log_level_printf(4,"ControlAPI.Execute filename:(%s)\n",filename.c_str());
	// tolower(filename)
	for(unsigned int i = 0; i < filename.length(); i++)
		filename[i] = tolower(filename[i]);

	// debugging informations
	if(CLogging::getInstance()->getDebug())
	{
		dprintf("Execute CGI : %s\n",filename.c_str());
		for(CStringList::iterator it = hh->ParamList.begin() ;
				it != hh->ParamList.end() ; it++)
			dprintf("  Parameter %s : %s\n",it->first.c_str(), it->second.c_str());
	}

	// get function index
	for(unsigned int i = 0; i < (sizeof(yCgiCallList)/sizeof(yCgiCallList[0])); i++)
		if (filename == yCgiCallList[i].func_name)
		{
			index = i;
			break;
		}
	if(index == -1) // function not found
	{
		hh->SetError(HTTP_NOT_IMPLEMENTED, HANDLED_NOT_IMPLEMENTED);
		return;
	}

	// send header
	else if(std::string(yCgiCallList[index].mime_type) == "")	// decide in function
		;
	else if(std::string(yCgiCallList[index].mime_type) == "+xml")		// Parameter xml?
		if ((hh->ParamList["xml"] != "") ||(hh->ParamList["format"] == "xml"))
			hh->SetHeader(HTTP_OK, "text/xml; charset=UTF-8");
		else
			hh->SetHeader(HTTP_OK, "text/html; charset=UTF-8");
	else
		hh->SetHeader(HTTP_OK, yCgiCallList[index].mime_type);
	// response
	hh->status = HANDLED_READY;
	if (hh->Method == M_HEAD)	// HEAD or function call
		return;
	else
	{
		(this->*yCgiCallList[index].pfunc)(hh);
		return;
	}
}

//=============================================================================
// CGI Functions
// CyhookHandler contains input/output abstractions
//=============================================================================
void CControlAPI::TimerCGI(CyhookHandler *hh)
{
	if (NeutrinoAPI->Timerd->isTimerdAvailable())
	{
		if (!hh->ParamList.empty() && hh->ParamList["format"].empty())
		{
			if (hh->ParamList["action"] == "new")
				doNewTimer(hh);
			else if (hh->ParamList["action"] == "modify")
				doModifyTimer(hh);
			else if (hh->ParamList["action"] == "remove")
			{
				unsigned removeId = atoi(hh->ParamList["id"].c_str());
				NeutrinoAPI->Timerd->removeTimerEvent(removeId);
				hh->SendOk();
			}
			else if(hh->ParamList["get"] != "")
			{
				int pre=0,post=0;
				NeutrinoAPI->Timerd->getRecordingSafety(pre,post);
				if(hh->ParamList["get"] == "pre")
					hh->printf("%d\n", pre);
				else if(hh->ParamList["get"] == "post")
					hh->printf("%d\n", post);
				else
					hh->SendError();
			}
		}
		else {
			if (hh->ParamList["format"] == "xml")
				SendTimersXML(hh);
			else
				SendTimers(hh);
		}
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::SetModeCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList.empty()))
	{
		if (hh->ParamList["1"] == "status")	// display recoding status
		{
			if (NeutrinoAPI->Zapit->isRecordModeActive())
				hh->WriteLn("on");
			else
				hh->WriteLn("off");
			return;
		}

		if (hh->ParamList["1"] == "radio")	// switch to radio mode
		{
			int mode = NeutrinoMessages::mode_radio;
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::CHANGEMODE, CEventServer::INITID_HTTPD, (void *)&mode,sizeof(int));
			sleep(1);
			NeutrinoAPI->UpdateBouquets();
		}
		else if (hh->ParamList["1"] == "tv")	// switch to tv mode
		{
			int mode = NeutrinoMessages::mode_tv;
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::CHANGEMODE, CEventServer::INITID_HTTPD, (void *)&mode,sizeof(int));
			sleep(1);
			NeutrinoAPI->UpdateBouquets();
		}
		else if (hh->ParamList["record"] == "start")	// start record mode
		{
			if(hh->ParamList["stopplayback"] == "true")
				NeutrinoAPI->Zapit->stopPlayBack();
			NeutrinoAPI->Sectionsd->setPauseScanning(true);
			NeutrinoAPI->Zapit->setRecordMode(true);
		}
		else if (hh->ParamList["record"] == "stop")	// stop record mode
		{
			NeutrinoAPI->Zapit->setRecordMode(false);
			NeutrinoAPI->Sectionsd->setPauseScanning(false);
			if (!NeutrinoAPI->Zapit->isPlayBackActive())
				NeutrinoAPI->Zapit->startPlayBack();
		}
		hh->SendOk();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::GetModeCGI(CyhookHandler *hh)
{
	int mode = NeutrinoAPI->Zapit->getMode();
	if ( mode == CZapitClient::MODE_TV)
		hh->WriteLn("tv");
	else if ( mode == CZapitClient::MODE_RADIO)
		return hh->WriteLn("radio");
	else
		return hh->WriteLn("unknown");
}

//-----------------------------------------------------------------------------
void CControlAPI::ExecCGI(CyhookHandler *hh)
{
	bool res = false;
	std::string script, result;
	// override standard header
	if (hh->ParamList.size() > 1 && hh->ParamList["xml"].empty())
		hh->SetHeader(HTTP_OK, "text/html; charset=UTF-8");
	else if (hh->ParamList.size() > 1 && !hh->ParamList["xml"].empty())
		hh->SetHeader(HTTP_OK, "text/xml; charset=UTF-8");
	else
		hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
	if (hh->ParamList.size() > 0)
	{
		script = hh->ParamList["1"];
		unsigned int len = hh->ParamList.size();
		for(unsigned int y=2; y<=len; y++)
			if(!hh->ParamList[itoa(y)].empty())
			{
				script += " ";
				script += hh->ParamList[itoa(y)];
			}
		result = YexecuteScript(hh, script);
	}
	else
		printf("[CControlAPI] no script given\n");

	res = (result != "error");
	if (res)
		hh->Write(result);
	else
		hh->SetError(HTTP_NOT_FOUND);
}

//-----------------------------------------------------------------------------
void CControlAPI::SystemCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList.empty()))
	{
		//FIXME: No system information until now
		hh->SendOk();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::StandbyCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList.empty()))
	{
		if (hh->ParamList["1"] == "on")	// standby mode on
		{
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::STANDBY_ON, CEventServer::INITID_HTTPD);
			hh->SendOk();
		}
		else if (hh->ParamList["1"] == "off")// standby mode off
		{
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::STANDBY_OFF, CEventServer::INITID_HTTPD);
			hh->SendOk();
		}
		else
			hh->SendError();
	}
	else
#if HAVE_DBOX2 // FIXME: not implemented
		if(NeutrinoAPI->Controld->getVideoPowerDown())
			hh->WriteLn("on");
		else
#endif
			hh->WriteLn("off");
}

//-----------------------------------------------------------------------------
void CControlAPI::RCCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList.empty()))
	{
		if (hh->ParamList["1"] == "lock")	// lock remote control
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::LOCK_RC, CEventServer::INITID_HTTPD);
		else if (hh->ParamList["1"] == "unlock")// unlock remote control
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::UNLOCK_RC, CEventServer::INITID_HTTPD);
		else
			hh->SendError();
	}
	hh->SendOk();
}

//-----------------------------------------------------------------------------
// Get actual Date
// security: strftime has buffer-overflow limit. ok!
//-----------------------------------------------------------------------------
void CControlAPI::GetDateCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty())
	{
		//paramlos
		char *timestr = new char[50];
		struct timeb tm;
		ftime(&tm);
		strftime(timestr, 20, "%d.%m.%Y\n", localtime(&tm.time) );
		hh->Write(timestr);
		delete[] timestr;
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
// Get actual Time
// security: strftime has buffer-overflow limit. ok!
//-----------------------------------------------------------------------------
void CControlAPI::GetTimeCGI(CyhookHandler *hh)
{
	time_t now = time(NULL);

	if (hh->ParamList.empty())
	{
		//paramlos
		char *timestr = new char[50];
		struct tm *tm = localtime(&now);
		strftime(timestr, 20, "%H:%M:%S\n", tm );
		hh->Write(timestr);
		delete[] timestr;
	}
	else if (hh->ParamList["1"].compare("rawtime") == 0)
		hh->printf("%ld\n",now);
	else
		hh->SendError();
}
//-----------------------------------------------------------------------------
// send services.xml
void CControlAPI::GetServicesxmlCGI(CyhookHandler *hh)
{
	hh->SendFile("/var/tuxbox/config/zapit/services.xml");
}

//-----------------------------------------------------------------------------
// send bouquets.xml
void CControlAPI::GetBouquetsxmlCGI(CyhookHandler *hh)
{
	hh->SendFile("/var/tuxbox/config/zapit/bouquets.xml");
}

//-----------------------------------------------------------------------------
// get actual channel_id
void CControlAPI::GetChannel_IDCGI(CyhookHandler *hh)
{
	CZapitClient::CCurrentServiceInfo current_pids = NeutrinoAPI->Zapit->getCurrentServiceInfo();
	hh->printf("%x%04x%04x\n",current_pids.tsid, current_pids.onid, current_pids.sid);
}

//-----------------------------------------------------------------------------
void CControlAPI::MessageCGI(CyhookHandler *hh)
{
	std::string message;
	int event = 0;

	if (!(hh->ParamList["popup"].empty()))
	{
		message = hh->ParamList["popup"];
		event = NeutrinoMessages::EVT_POPUP;
	}
	else if (!(hh->ParamList["nmsg"].empty()))
	{
		message = hh->ParamList["nmsg"];
		event = NeutrinoMessages::EVT_EXTMSG;
	}
	else
	{
		hh->SendError();
		return;
	}

	if (event != 0)
	{
		message=decodeString(message);
		NeutrinoAPI->EventServer->sendEvent(event, CEventServer::INITID_HTTPD, (void *) message.c_str(), message.length() + 1);
		hh->SendOk();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::InfoCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty())
		hh->Write("Neutrino HD\n");
	else
	{
		if (hh->ParamList["1"] == "streaminfo")		// print streaminfo
			SendStreamInfo(hh);
		else if (hh->ParamList["1"] == "version")	// send version file
			hh->SendFile("/.version");
		else if (hh->ParamList["1"] == "httpdversion")	// print httpd version typ (only ffor comptibility)
			hh->Write("3");
		else if (hh->ParamList["1"] == "nhttpd_version")// print nhttpd version
			hh->printf("%s\n", HTTPD_VERSION);
		else if (hh->ParamList["1"] == "hwinfo")// print hwinfo
			HWInfoCGI(hh);
		else
			hh->SendError();
	}
}

void CControlAPI::HWInfoCGI(CyhookHandler *hh)
{
	unsigned int system_rev = cs_get_revision();
	std::string  boxname;
	static CNetAdapter netadapter; 
	std::string eth_id = netadapter.getMacAddr();
	std::transform(eth_id.begin(), eth_id.end(), eth_id.begin(), ::tolower);

	if("00:c5:5c" == eth_id.substr(0, 8) )
		boxname = "Coolstream ";
	else if("ba:dd:ad"  == eth_id.substr(0, 8) )
		boxname = "Armas ";

	switch(system_rev)
	{
	case 1:
		if( boxname == "Armas ")
			boxname += "TripleDragon";
		break;
	  case 6:
		boxname += "HD1";
		break;
	case 7:
		boxname += "BSE";
		break;
	case 8:
	case 9:
		boxname += "Neo";
		break;
	case 10:
		boxname += "Zee";
		break;

	default: {
		char buffer[10];
		snprintf(buffer, sizeof(buffer), "%u\n", system_rev);
		boxname += "Unknown nr. ";
		boxname += buffer;
	}
	break;
	}
	boxname += (g_info.delivery_system == DVB_S || (system_rev == 1)) ? " SAT":" CABLE";
	hh->printf("%s\nMAC:%s\n", boxname.c_str(),eth_id.c_str());


}
//-----------------------------------------------------------------------------
void CControlAPI::ShutdownCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty())
	{
		NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::SHUTDOWN, CEventServer::INITID_HTTPD);
		hh->SendOk();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::RebootCGI(CyhookHandler *hh)
{
	FILE *f = fopen("/tmp/.reboot", "w");
	fclose(f);
	return ShutdownCGI(hh);
}

//-----------------------------------------------------------------------------
int CControlAPI::rc_send(int ev, unsigned int code, unsigned int value)
{
	struct input_event iev;
	iev.type=EV_KEY;
	iev.code=code;
	iev.value=value;
	return write(ev,&iev,sizeof(iev));
}

//-----------------------------------------------------------------------------
// security: use const char-Pointers
struct key {
	const char *name;
	const int code;
};

#ifndef KEY_TOPLEFT
#define KEY_TOPLEFT	0x1a2
#endif

#ifndef KEY_TOPRIGHT
#define KEY_TOPRIGHT	0x1a3
#endif

#ifndef KEY_BOTTOMLEFT
#define KEY_BOTTOMLEFT	0x1a4
#endif

#ifndef KEY_BOTTOMRIGHT
#define KEY_BOTTOMRIGHT	0x1a5
#endif

static const struct key keynames[] = {
	{"KEY_POWER",		KEY_POWER},
	{"KEY_MUTE",		KEY_MUTE},
	{"KEY_1",			KEY_1},
	{"KEY_2",			KEY_2},
	{"KEY_3",			KEY_3},
	{"KEY_4",			KEY_4},
	{"KEY_5",			KEY_5},
	{"KEY_6",			KEY_6},
	{"KEY_7",			KEY_7},
	{"KEY_8",			KEY_8},
	{"KEY_9",			KEY_9},
	{"KEY_0",			KEY_0},
	{"KEY_INFO",		KEY_INFO},
	{"KEY_MODE",		KEY_MODE},
	{"KEY_SETUP",		KEY_MENU},
	{"KEY_EPG",			KEY_EPG},
	{"KEY_FAVORITES",	KEY_FAVORITES},
	{"KEY_HOME",		KEY_EXIT},
	{"KEY_UP",			KEY_UP},
	{"KEY_LEFT",		KEY_LEFT},
	{"KEY_OK",			KEY_OK},
	{"KEY_RIGHT",		KEY_RIGHT},
	{"KEY_DOWN",		KEY_DOWN},
	{"KEY_VOLUMEUP",	KEY_VOLUMEUP},
	{"KEY_VOLUMEDOWN",	KEY_VOLUMEDOWN},
	{"KEY_PAGEUP",		KEY_PAGEUP},
	{"KEY_PAGEDOWN",	KEY_PAGEDOWN},
	{"KEY_TV",			KEY_TV},
	{"KEY_TEXT",		KEY_TEXT},
	{"KEY_RADIO",		KEY_RADIO},
	{"KEY_RED",			KEY_RED},
	{"KEY_GREEN",		KEY_GREEN},
	{"KEY_YELLOW",		KEY_YELLOW},
	{"KEY_BLUE",		KEY_BLUE},
	{"KEY_SAT",			KEY_SAT},
	{"KEY_HELP",		KEY_HELP},
	{"KEY_NEXT",		KEY_NEXT},
	{"KEY_PREVIOUS",	KEY_PREVIOUS},
	{"KEY_TIME", 		KEY_TIME},
	{"KEY_AUDIO",		KEY_AUDIO},
	{"KEY_REWIND",		KEY_REWIND},
	{"KEY_FORWARD",		KEY_FORWARD},
	{"KEY_PAUSE",		KEY_PAUSE},
	{"KEY_RECORD",		KEY_RECORD},
	{"KEY_STOP",		KEY_STOP},
	{"KEY_PLAY",		KEY_PLAY}
};

// The code here is based on rcsim. Thx Carjay!
void CControlAPI::RCEmCGI(CyhookHandler *hh) {
	if (hh->ParamList.empty()) {
		hh->SendError();
		return;
	}
	std::string keyname = hh->ParamList["1"];
	int sendcode = -1;
	for (unsigned int i = 0; sendcode == -1 && i < sizeof(keynames)
			/ sizeof(key); i++) {
		if (!strcmp(keyname.c_str(), keynames[i].name))
			sendcode = keynames[i].code;
	}

	if (sendcode == -1) {
		printf("[nhttpd] Key %s not found\n", keyname.c_str());
		hh->SendError();
		return;
	}
	unsigned int repeat = 1;
	unsigned int delay = 250;
	if (hh->ParamList["delay"] != "")
		delay = atoi(hh->ParamList["delay"].c_str());
	if (hh->ParamList["duration"] != "")
		repeat = atoi(hh->ParamList["duration"].c_str()) * 1000 / delay;
	if (hh->ParamList["repeat"] != "")
		repeat = atoi(hh->ParamList["repeat"].c_str());

	int evd = open(EVENTDEV, O_RDWR);
	if (evd < 0) {
		hh->SendError();
		perror("opening event0 failed");
		return;
	}
	if (rc_send(evd, sendcode, KEY_PRESSED) < 0) {
		perror("writing 'KEY_PRESSED' event failed");
		hh->SendError();
		close(evd);
		return;
	}
	if (rc_send(evd, sendcode, KEY_RELEASED) < 0) {
		perror("writing 'KEY_RELEASED' event failed");
		close(evd);
		hh->SendError();
		return;
	}
	close(evd);
	hh->SendOk();
}
//-----------------------------------------------------------------------------
void CControlAPI::AspectRatioCGI(CyhookHandler *hh)
{
	hh->printf("%s",(NeutrinoAPI->getVideoAspectRatioAsString()).c_str());
}
//-----------------------------------------------------------------------------
void CControlAPI::VideoFormatCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty() || hh->ParamList["1"] == "status") {
		hh->printf("%s",(NeutrinoAPI->getVideoAspectRatioAsString()).c_str());
		return;
	}
	if (NeutrinoAPI->setVideoAspectRatioAsString(hh->ParamList["1"]) != -1)
		hh->SendOk();
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::VideoOutputCGI(CyhookHandler *hh)
{
// FIXME: not implemented
	hh->SendOk();
}

//-----------------------------------------------------------------------------
void CControlAPI::VCROutputCGI(CyhookHandler *hh)
{
// FIXME: not implemented
	hh->SendOk();
}

//-----------------------------------------------------------------------------
void CControlAPI::ScartModeCGI(CyhookHandler *hh)
{
// FIXME: not implemented
	hh->SendOk();
}

//-----------------------------------------------------------------------------
void CControlAPI::AudioCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty() || hh->ParamList["1"] == "info") {
		hh->printf("%s",(NeutrinoAPI->getAudioInfoAsString()).c_str());
		return;
	}
	//TODO: more
}

//-------------------------------------------------------------------------
void CControlAPI::VolumeCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty()) {//without param: show actual volumen
		unsigned int volume;
		NeutrinoAPI->Zapit->getVolume(&volume, &volume);
		hh->printf("%d", volume);
	}
	else if (hh->ParamList["1"].compare("mute") == 0)
	{
		NeutrinoAPI->Zapit->muteAudio(true);
		hh->SendOk();
	}
	else if (hh->ParamList["1"].compare("unmute") == 0)
	{
		NeutrinoAPI->Zapit->muteAudio(false);
		hh->SendOk();
	}
	else if (hh->ParamList["1"].compare("status") == 0) { // Mute status
		(NeutrinoAPI->Zapit->getMuteStatus()) ? hh->Write("1") :  hh->Write("0");
	}
	else if(hh->ParamList["1"]!="") { //set volume
		char vol = atol( hh->ParamList["1"].c_str() );
		NeutrinoAPI->Zapit->setVolume(vol,vol);
		hh->SendOk();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::CryptCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty() || hh->ParamList["1"] == "info") {
		hh->printf("%s",(NeutrinoAPI->getCryptInfoAsString()).c_str());
		return;
	}
	//TODO: more
}

//-----------------------------------------------------------------------------
void CControlAPI::ChannellistCGI(CyhookHandler *hh)
{
	SendChannelList(hh);
}

//-----------------------------------------------------------------------------
void CControlAPI::_GetBouquetWriteItem(CyhookHandler *hh, CZapitChannel * channel, int bouquetNr, int nr) {
	if (hh->ParamList["format"] == "json") {
		hh->printf("\t\t{'number': '%u', 'id': '"
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "', 'short_id': '"
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "', 'name': '%s', logo: '%s', bouquetnr: '%d'}\n",
			   nr,
			   channel->channel_id,
			   channel->channel_id&0xFFFFFFFFFFFFULL,
			   channel->getName().c_str(),
			   NeutrinoAPI->getLogoFile(hh->WebserverConfigList["Tuxbox.LogosURL"], channel->channel_id).c_str(),
			   bouquetNr
			  );
	}
	else if((hh->ParamList["format"] == "xml") || !(hh->ParamList["xml"].empty()) ) {
		hh->printf("<channel>\n\t<number>%u</number>\n\t<bouquet>%d</bouquet>\n\t<id>"
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "</id>\n\t<short_id>"
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "</short_id>\n\t<name><![CDATA[%s]]></name>\n<logo><![CDATA[%s]]></logo>\n</channel>\n",
			   nr,
			   bouquetNr,
			   channel->channel_id,
			   channel->channel_id&0xFFFFFFFFFFFFULL,
			   channel->getName().c_str(),
			   NeutrinoAPI->getLogoFile(hh->WebserverConfigList["Tuxbox.LogosURL"], channel->channel_id).c_str()
			  );
	}
	else {
		hh->printf("%u "
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   " %s\n",
			   nr,
			   channel->channel_id,
			   channel->getName().c_str());
	}
}
//-----------------------------------------------------------------------------
void CControlAPI::GetBouquetCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList.empty()))
	{
		int mode = CZapitClient::MODE_CURRENT;

		if (!(hh->ParamList["mode"].empty()))
		{
			if (hh->ParamList["mode"].compare("TV") == 0)
				mode = CZapitClient::MODE_TV;
			if (hh->ParamList["mode"].compare("RADIO") == 0)
				mode = CZapitClient::MODE_RADIO;
		}

		// Get Bouquet Number. First matching current channel
		if (hh->ParamList["1"] == "actual")
		{
			int actual=0;
			for (int i = 0; i < (int) g_bouquetManager->Bouquets.size(); i++) {
				if(g_bouquetManager->existsChannelInBouquet(i, live_channel_id)) {
					actual=i+1;
					break;
				}
			}
			hh->printf("%d",actual);
		}
		else
		{
			// write header
			if (hh->ParamList["format"] == "json") {
				hh->WriteLn("{");
			}
			else if((hh->ParamList["format"] == "xml") || !(hh->ParamList["xml"].empty()) ) {
				hh->WriteLn("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
				hh->WriteLn("<channellist>");
//				hh->printf("<bouquet>\n\t<bnumber>%s</bnumber>\n</bouquet>\n",hh->ParamList["bouquet"].c_str());
			}

			ZapitChannelList channels;
			if(hh->ParamList["bouquet"] != "") {
				// list for given bouquet
				int BouquetNr = atoi(hh->ParamList["bouquet"].c_str());
				if(BouquetNr > 0)
					BouquetNr--;
				channels = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->Bouquets[BouquetNr]->radioChannels : g_bouquetManager->Bouquets[BouquetNr]->tvChannels;
				int num = 1 + (mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin().getNrofFirstChannelofBouquet(BouquetNr) : g_bouquetManager->tvChannelsBegin().getNrofFirstChannelofBouquet(BouquetNr)) ;
				for(int j = 0; j < (int) channels.size(); j++) {
					CZapitChannel * channel = channels[j];
					_GetBouquetWriteItem(hh, channel, BouquetNr, num+j);
				}
			} else {
				// list all
				for (int i = 0; i < (int) g_bouquetManager->Bouquets.size(); i++) {
					/*
									CBouquetManager::ChannelIterator cit = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin() : g_bouquetManager->tvChannelsBegin();
									for (; !(cit.EndOfChannels()); cit++) {
										CZapitChannel * channel = *cit;*/
					channels = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->Bouquets[i]->radioChannels : g_bouquetManager->Bouquets[i]->tvChannels;
					int num = 1 + (mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin().getNrofFirstChannelofBouquet(i) : g_bouquetManager->tvChannelsBegin().getNrofFirstChannelofBouquet(i)) ;
					for(int j = 0; j < (int) channels.size(); j++) {
						CZapitChannel * channel = channels[j];
						_GetBouquetWriteItem(hh, channel, i, num+j);
					}
				}
			}

			// write footer
			if (hh->ParamList["format"] == "json")
				hh->WriteLn("}");
			else if((hh->ParamList["format"] == "xml") || !(hh->ParamList["xml"].empty()) )
				hh->WriteLn("</channellist>");
		}
	}
	else
		hh->WriteLn("error");
}

//-----------------------------------------------------------------------------
//	Return all bouquets
//-----------------------------------------------------------------------------
//	Parameter: 	format = <empty>|json|xml (optional) -> output type
//				showhidden = true (default) | false -> show hidden bouquets
//				encode = true | false (default) use URLencode
//	Result:
//				bouquet number, bouquet name
//-----------------------------------------------------------------------------
void CControlAPI::GetBouquetsCGI(CyhookHandler *hh)
{
	bool show_hidden = true;
	bool encode = false;

	if (hh->ParamList["showhidden"] == "false")
		show_hidden = false;

	if (hh->ParamList["encode"] == "true")
		encode = true;

	// write header
	if (hh->ParamList["format"] == "json")
		hh->WriteLn("{");
	else if (hh->ParamList["format"] == "xml") {
		hh->WriteLn("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
		hh->WriteLn("<bouquets>");
	}

	int mode = NeutrinoAPI->Zapit->getMode();
	std::string bouquet;
	for (int i = 0; i < (int) g_bouquetManager->Bouquets.size(); i++) {
		ZapitChannelList * channels = mode == CZapitClient::MODE_RADIO ? &g_bouquetManager->Bouquets[i]->radioChannels : &g_bouquetManager->Bouquets[i]->tvChannels;
		if(!channels->empty() && (!g_bouquetManager->Bouquets[i]->bHidden || show_hidden)) {
			bouquet = std::string(g_bouquetManager->Bouquets[i]->bFav ? g_Locale->getText(LOCALE_FAVORITES_BOUQUETNAME) :g_bouquetManager->Bouquets[i]->Name.c_str());
			if(encode)
				bouquet = encodeString(bouquet); // encode (URLencode) the bouquetname

			if (hh->ParamList["format"] == "json")
				hh->printf("{number: %u, name: %s}\n", i + 1, bouquet.c_str() );
			else if (hh->ParamList["format"] == "xml")
				hh->printf("\t<bouquet>\n\t\t<number>%u</number>\n\t\t<name>%s</name>\n\t</bouquet>\n", i + 1, bouquet.c_str() );
			else
				hh->printf("%u %s\n", i + 1, bouquet.c_str() );
		}
	}
	// write footer
	if (hh->ParamList["format"] == "json")
		hh->WriteLn("}");
	else if (hh->ParamList["format"] == "xml")
		hh->WriteLn("</bouquets>");
}
//-----------------------------------------------------------------------------
//	details EPG Information for channelid
//-----------------------------------------------------------------------------
void CControlAPI::channelEPGAsXML(CyhookHandler *hh, int bouquetnr, t_channel_id channel_id, int max, long stoptime)
{
	sectionsd_getEventsServiceKey(channel_id, NeutrinoAPI->eList);
	hh->printf("<channel_id>"
		   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
		   "</channel_id>\r\n", channel_id);
	hh->printf("<channel_short_id>"
		   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
		   "</channel_short_id>\r\n", channel_id&0xFFFFFFFFFFFFULL);
	hh->printf("<channel_name><![CDATA[%s]]></channel_name>\r\n", NeutrinoAPI->GetServiceName(channel_id).c_str());

	int i=0;
	CChannelEventList::iterator eventIterator;
	for (eventIterator = NeutrinoAPI->eList.begin(); eventIterator != NeutrinoAPI->eList.end(); eventIterator++, i++)
	{
		if( (max != -1 && i >= max) || ( stoptime != -1 && eventIterator->startTime >= stoptime))
			break;
		hh->WriteLn("<prog>");
		hh->printf("\t<bouquetnr>%d</bouquetnr>\r\n", bouquetnr);
		hh->printf("\t<channel_id>"
			   PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "</channel_id>\r\n", channel_id);
		hh->printf("\t<eventid>%llu</eventid>\r\n", eventIterator->eventID);
		hh->printf("\t<eventid_hex>%llx</eventid_hex>\r\n", eventIterator->eventID);
		hh->printf("\t<start_sec>%ld</start_sec>\r\n", eventIterator->startTime);
		char zbuffer[25] = {0};
		struct tm *mtime = localtime(&eventIterator->startTime);
		strftime(zbuffer,20,"%H:%M",mtime);
		hh->printf("\t<start_t>%s</start_t>\r\n", zbuffer);
		memset(zbuffer,0,sizeof(zbuffer));
		strftime(zbuffer,20,"%d.%m.%Y",mtime);
		hh->printf("\t<date>%s</date>\r\n", zbuffer);
		hh->printf("\t<stop_sec>%ld</stop_sec>\r\n", eventIterator->startTime+eventIterator->duration);
		long _stoptime = eventIterator->startTime+eventIterator->duration;
		mtime = localtime(&_stoptime);
		strftime(zbuffer,20,"%H:%M",mtime);
		hh->printf("\t<stop_t>%s</stop_t>\r\n", zbuffer);
		hh->printf("\t<duration_min>%d</duration_min>\r\n", (int)(eventIterator->duration/60));
		hh->printf("\t<description><![CDATA[%s]]></description>\r\n", eventIterator->description.c_str());

		if (!(hh->ParamList["details"].empty()))
		{
			CShortEPGData epg;
			if (sectionsd_getEPGidShort(eventIterator->eventID,&epg))
			{
				hh->printf("\t<info1><![CDATA[%s]]></info1>\r\n",epg.info1.c_str());
				hh->printf("\t<info2><![CDATA[%s]]></info2>\r\n",epg.info2.c_str());
			}
		}
		hh->WriteLn("</prog>");
	}
}

//-----------------------------------------------------------------------------
void CControlAPI::EpgCGI(CyhookHandler *hh)
{
	//hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
	NeutrinoAPI->eList.clear();
	if (hh->ParamList.empty())
	{
		hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
		CChannelEvent *event;
		NeutrinoAPI->GetChannelEvents();

		int mode = NeutrinoAPI->Zapit->getMode();
		CBouquetManager::ChannelIterator cit = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin() : g_bouquetManager->tvChannelsBegin();
		for (; !(cit.EndOfChannels()); cit++) {
			CZapitChannel * channel = *cit;
			event = NeutrinoAPI->ChannelListEvents[channel->channel_id];
			if(event)
				hh->printf(PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
					   " %llu %s\n",
					   channel->channel_id,
					   event->eventID,
					   event->description.c_str());
		}
	}
	else if (hh->ParamList["xml"].empty())
	{
		hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
		if (hh->ParamList["1"] == "ext")
		{
			CChannelEvent *event;
			NeutrinoAPI->GetChannelEvents();
			int mode = NeutrinoAPI->Zapit->getMode();
			CBouquetManager::ChannelIterator cit = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin() : g_bouquetManager->tvChannelsBegin();
			for (; !(cit.EndOfChannels()); cit++) {
				CZapitChannel * channel = *cit;
				event = NeutrinoAPI->ChannelListEvents[channel->channel_id];
				if(event)
					hh->printf(PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
						   " %ld %u %llu %s\n",
						   channel->channel_id,
						   event->startTime,
						   event->duration,
						   event->eventID,
						   event->description.c_str());
			}
		}
		else if (hh->ParamList["eventid"] != "")
		{
			//special epg query
			uint64_t epgid;
			sscanf( hh->ParamList["eventid"].c_str(), "%llu", &epgid);
			CShortEPGData epg;
			//if (NeutrinoAPI->Sectionsd->getEPGidShort(epgid,&epg))
			if (sectionsd_getEPGidShort(epgid,&epg))
			{
				hh->WriteLn(epg.title);
				hh->WriteLn(epg.info1);
				hh->WriteLn(epg.info2);
			}
		}
		else if (hh->ParamList["eventid2fsk"] != "")
		{
			if (hh->ParamList["starttime"] != "")
			{
				uint64_t epgid;
				time_t starttime;
				sscanf( hh->ParamList["fskid"].c_str(), "%llu", &epgid);
				sscanf( hh->ParamList["starttime"].c_str(), "%lu", &starttime);
				CEPGData longepg;
				if(sectionsd_getEPGid(epgid, starttime, &longepg))
				{
					hh->printf("%u\n", longepg.fsk);
					return;
				}
			}
			hh->SendError();
		}
		else if (!(hh->ParamList["id"].empty()))
		{
			t_channel_id channel_id;
			sscanf(hh->ParamList["id"].c_str(),
			       SCANF_CHANNEL_ID_TYPE,
			       &channel_id);
			sectionsd_getEventsServiceKey(channel_id, NeutrinoAPI->eList);
			CChannelEventList::iterator eventIterator;
			for (eventIterator = NeutrinoAPI->eList.begin(); eventIterator != NeutrinoAPI->eList.end(); eventIterator++)
			{
				CShortEPGData epg;
				if (sectionsd_getEPGidShort(eventIterator->eventID,&epg))
				{
					hh->printf("%llu %ld %d\n", eventIterator->eventID, eventIterator->startTime, eventIterator->duration);
					hh->printf("%s\n",epg.title.c_str());
					hh->printf("%s\n",epg.info1.c_str());
					hh->printf("%s\n\n",epg.info2.c_str());
				}
			}
		}
		else
		{
			//eventlist for a chan
			t_channel_id channel_id;
			sscanf(hh->ParamList["1"].c_str(),
			       SCANF_CHANNEL_ID_TYPE,
			       &channel_id);
			SendEventList(hh, channel_id);
		}
	}
	//	xml=true&channelid=<channel_id>|channelname=<channel name>[&details=true][&max=<max items>][&stoptime=<long:stop time>]
	//	details=true : Show EPG Info1 and info2
	//	stoptime : show only items until stoptime reached
	else if (!(hh->ParamList["xml"].empty()))
	{
		hh->SetHeader(HTTP_OK, "text/xml; charset=UTF-8");
		// max = maximal output items
		int max = -1;
		if (!(hh->ParamList["max"].empty()))
			max = atoi( hh->ParamList["max"].c_str() );

		// stoptime = maximal output items until starttime >= stoptime
		long stoptime = -1;
		if (!(hh->ParamList["stoptime"].empty()))
			stoptime = atol( hh->ParamList["stoptime"].c_str() );

		// determine channelid
		t_channel_id channel_id = (t_channel_id)-1;
		if (!(hh->ParamList["channelid"].empty()))
		{
			sscanf(hh->ParamList["channelid"].c_str(),
			       SCANF_CHANNEL_ID_TYPE,
			       &channel_id);
		}
		else if (!(hh->ParamList["channelname"].empty()))
		{
			channel_id = NeutrinoAPI->ChannelNameToChannelId( hh->ParamList["channelname"].c_str() );
		}
		// or determine bouquetnr -> iterate the bouquet
		int bouquetnr = -1;
		bool all_bouquets = false;

		if(hh->ParamList["bouquetnr"] == "all")
			all_bouquets = true;
		else if (!(hh->ParamList["bouquetnr"].empty()))
		{
			bouquetnr = atoi( hh->ParamList["bouquetnr"].c_str() );
			bouquetnr--;
		}

		hh->WriteLn("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
		hh->WriteLn("<epglist>");

		if(bouquetnr>=0)
		{
			// list for given bouquet
			ZapitChannelList channels;
			int mode = NeutrinoAPI->Zapit->getMode();

			channels = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->Bouquets[bouquetnr]->radioChannels : g_bouquetManager->Bouquets[bouquetnr]->tvChannels;
			std::string bouquet = std::string(g_bouquetManager->Bouquets[bouquetnr]->bFav ? g_Locale->getText(LOCALE_FAVORITES_BOUQUETNAME) :g_bouquetManager->Bouquets[bouquetnr]->Name.c_str());
			bouquet = encodeString(bouquet); // encode (URLencode) the bouquetname
			hh->printf("\t<bouquet>\n\t\t<number>%d</number>\n\t\t<name><![CDATA[%s]]></name>\n\t", bouquetnr+1, bouquet.c_str());

			for(int j = 0; j < (int) channels.size(); j++) {
				CZapitChannel * channel = channels[j];
				hh->WriteLn("\t\t<channel>");
				channelEPGAsXML(hh, bouquetnr+1, channel->channel_id, max, stoptime);
				hh->WriteLn("\t\t</channel>");
			}
			hh->WriteLn("\t</bouquet>");
		}
		else if(all_bouquets)
		{
			// list all bouquets			if(encode)
			ZapitChannelList channels;
			int mode = NeutrinoAPI->Zapit->getMode();

			for (int i = 0; i < (int) g_bouquetManager->Bouquets.size(); i++) {
				channels = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->Bouquets[i]->radioChannels : g_bouquetManager->Bouquets[i]->tvChannels;
				std::string bouquet = std::string(g_bouquetManager->Bouquets[i]->bFav ? g_Locale->getText(LOCALE_FAVORITES_BOUQUETNAME) :g_bouquetManager->Bouquets[i]->Name.c_str());
				bouquet = encodeString(bouquet); // encode (URLencode) the bouquetname
				hh->printf("\t<bouquet>\n\t\t<number>%d</number>\n\t\t<name><![CDATA[%s]]></name>\n\t", i+1, bouquet.c_str());

				for(int j = 0; j < (int) channels.size(); j++) {
					CZapitChannel * channel = channels[j];
					hh->WriteLn("\t\t<channel>");
					channelEPGAsXML(hh, j+1, channel->channel_id, max, stoptime);
					hh->WriteLn("\t\t</channel>");
				}
				hh->WriteLn("\t</bouquet>");
			}
		}
		else
			// list one channel
			channelEPGAsXML(hh, 0, channel_id, max, stoptime);
		hh->WriteLn("</epglist>");
	}
}

//-----------------------------------------------------------------------------
void CControlAPI::VersionCGI(CyhookHandler *hh)
{
	hh->SendFile("/.version");
}
//-----------------------------------------------------------------------------
void CControlAPI::ReloadNutrinoSetupfCGI(CyhookHandler *hh)
{
	NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::RELOAD_SETUP, CEventServer::INITID_HTTPD);
	hh->SendOk();
}


//-----------------------------------------------------------------------------
void CControlAPI::ZaptoCGI(CyhookHandler *hh)
{
	if (hh->ParamList.empty())
	{
		hh->printf(PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   "\n",
			   NeutrinoAPI->Zapit->getCurrentServiceID());
		return;
	}
	else
	{
		if (hh->ParamList["1"] == "getpids")		// getpids !
			SendcurrentVAPid(hh);
		else if (hh->ParamList["1"] == "getallpids")		// getpids !
			SendAllCurrentVAPid(hh);
		else if (hh->ParamList["1"] == "stopplayback")
		{
			NeutrinoAPI->Zapit->stopPlayBack();
			NeutrinoAPI->Sectionsd->setPauseScanning(true);
			hh->SendOk();
		}
		else if (hh->ParamList["1"] == "startplayback")
		{
			NeutrinoAPI->Zapit->startPlayBack();
			NeutrinoAPI->Sectionsd->setPauseScanning(false);
			dprintf("start playback requested..\n");
			hh->SendOk();
		}
		else if (hh->ParamList["1"] == "statusplayback")
			hh->Write((char *) (NeutrinoAPI->Zapit->isPlayBackActive() ? "1" : "0"));
		else if (hh->ParamList["1"] == "stopsectionsd")
		{
			NeutrinoAPI->Sectionsd->setPauseScanning(true);
			hh->SendOk();
		}
		else if (hh->ParamList["1"] == "startsectionsd")
		{
			NeutrinoAPI->Sectionsd->setPauseScanning(false);
			hh->SendOk();
		}
		else if (hh->ParamList["1"] == "statussectionsd")
			hh->Write((char *) (NeutrinoAPI->Sectionsd->getIsScanningActive() ? "1" : "0"));
		else if (hh->ParamList["1"] == "getallsubchannels")
		{
			t_channel_id current_channel = NeutrinoAPI->Zapit->getCurrentServiceID();
			CSectionsdClient::LinkageDescriptorList desc;
			CSectionsdClient::responseGetCurrentNextInfoChannelID currentNextInfo;
			sectionsd_getCurrentNextServiceKey(current_channel&0xFFFFFFFFFFFFULL, currentNextInfo);
			if (sectionsd_getLinkageDescriptorsUniqueKey(currentNextInfo.current_uniqueKey,desc))
			{
				for(unsigned int i=0; i< desc.size(); i++)
				{
					t_channel_id sub_channel_id =
						CREATE_CHANNEL_ID_FROM_SERVICE_ORIGINALNETWORK_TRANSPORTSTREAM_ID(
							desc[i].serviceId, desc[i].originalNetworkId, desc[i].transportStreamId);
					hh->printf(PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
						   " %s\n",
						   sub_channel_id,
						   (desc[i].name).c_str());
				}
			}
		}
		else if (hh->ParamList["name"] != "")
		{
			t_channel_id channel_id;
			channel_id = NeutrinoAPI->ChannelNameToChannelId(hh->ParamList["name"]);
			if(channel_id != (t_channel_id)-1)
			{
				NeutrinoAPI->ZapToChannelId(channel_id);
				hh->SendOk();
			}
			else
				hh->SendError();
		}
		else
		{
			NeutrinoAPI->ZapTo(hh->ParamList["1"].c_str());

			hh->SendOk();
		}
		return;
	}
	hh->SendError();
	return;
}

//-----------------------------------------------------------------------------
void CControlAPI::StartPluginCGI(CyhookHandler *hh)
{
	std::string pluginname;
	if (!(hh->ParamList.empty()))
	{
		if (hh->ParamList["name"] != "")
		{
			pluginname = hh->ParamList["name"];
			pluginname=decodeString(pluginname);
			NeutrinoAPI->EventServer->sendEvent(NeutrinoMessages::EVT_START_PLUGIN,
							    CEventServer::INITID_HTTPD,
							    (void *) pluginname.c_str(),
							    pluginname.length() + 1);

			hh->SendOk();
		}
		else
			hh->SendError();
	}
	else
		hh->SendError();
}

//-----------------------------------------------------------------------------
void CControlAPI::LCDAction(CyhookHandler *hh)
{
	hh->SendOk();
}

//-------------------------------------------------------------------------
// Send functions (for ExecuteCGI)
//-------------------------------------------------------------------------
void CControlAPI::SendEventList(CyhookHandler *hh, t_channel_id channel_id)
{
	int pos;
	sectionsd_getEventsServiceKey(channel_id, NeutrinoAPI->eList);
	CChannelEventList::iterator eventIterator;

	for (eventIterator = NeutrinoAPI->eList.begin(); eventIterator != NeutrinoAPI->eList.end(); eventIterator++, pos++)
		hh->printf("%llu %ld %d %s\n", eventIterator->eventID, eventIterator->startTime, eventIterator->duration, eventIterator->description.c_str());
}

//-----------------------------------------------------------------------------
void CControlAPI::SendChannelList(CyhookHandler *hh)
{
	int mode = NeutrinoAPI->Zapit->getMode();
	hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
	CBouquetManager::ChannelIterator cit = mode == CZapitClient::MODE_RADIO ? g_bouquetManager->radioChannelsBegin() : g_bouquetManager->tvChannelsBegin();
	for (; !(cit.EndOfChannels()); cit++) {
		CZapitChannel * channel = *cit;
		hh->printf(PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS
			   " %s\n",
			   channel->channel_id,
			   channel->getName().c_str());
	}
}

//-----------------------------------------------------------------------------
void CControlAPI::SendStreamInfo(CyhookHandler *hh)
{
	hh->WriteLn(NeutrinoAPI->getVideoResolutionAsString());
	hh->WriteLn(NeutrinoAPI->getVideoAspectRatioAsString());
	hh->WriteLn(NeutrinoAPI->getVideoFramerateAsString());
	hh->WriteLn(NeutrinoAPI->getAudioInfoAsString());
}

//-----------------------------------------------------------------------------
void CControlAPI::SendcurrentVAPid(CyhookHandler *hh)
{
	CZapitClient::responseGetPIDs pids;
	pids.PIDs.vpid=0;
	NeutrinoAPI->Zapit->getPIDS(pids);

	hh->printf("%u\n", pids.PIDs.vpid);
	if(!pids.APIDs.empty())
		hh->printf("%u\n", pids.APIDs[0].pid);
	else
		hh->printf("0\n");
}

//-----------------------------------------------------------------------------
void CControlAPI::SendAllCurrentVAPid(CyhookHandler *hh)
{
	static bool init_iso=true;
	if(init_iso)
	{
		if(_initialize_iso639_map())
			init_iso=false;
	}
	bool eit_not_ok=true;
	CZapitClient::responseGetPIDs pids;

	CSectionsdClient::ComponentTagList tags;
	pids.PIDs.vpid=0;
	NeutrinoAPI->Zapit->getPIDS(pids);

	hh->printf("%05u\n", pids.PIDs.vpid);

	t_channel_id current_channel = NeutrinoAPI->Zapit->getCurrentServiceID();
	CSectionsdClient::responseGetCurrentNextInfoChannelID currentNextInfo;
	sectionsd_getCurrentNextServiceKey(current_channel&0xFFFFFFFFFFFFULL, currentNextInfo);
	if (sectionsd_getComponentTagsUniqueKey(currentNextInfo.current_uniqueKey,tags))
	{
		for (unsigned int i=0; i< tags.size(); i++)
		{
			for (unsigned short j=0; j< pids.APIDs.size(); j++)
			{
				if ( pids.APIDs[j].component_tag == tags[i].componentTag )
				{
					if(!tags[i].component.empty())
					{
						if(!(isalnum(tags[i].component[0])))
							tags[i].component=tags[i].component.substr(1,tags[i].component.length()-1);
						hh->printf("%05u %s\n",pids.APIDs[j].pid,tags[i].component.c_str());
					}
					else
					{
						if(!(init_iso))
						{
							strcpy( pids.APIDs[j].desc, _getISO639Description( pids.APIDs[j].desc ) );
						}
						hh->printf("%05u %s %s\n",pids.APIDs[j].pid,pids.APIDs[j].desc,pids.APIDs[j].is_ac3 ? " (AC3)": " ");
					}
					eit_not_ok=false;
					break;
				}
			}
		}
	}
	if(eit_not_ok)
	{
		unsigned short i = 0;
		for (CZapitClient::APIDList::iterator it = pids.APIDs.begin(); it!=pids.APIDs.end(); it++)
		{
			if(!(init_iso))
			{
				strcpy( pids.APIDs[i].desc, _getISO639Description( pids.APIDs[i].desc ) );
			}
			hh->printf("%05u %s %s\n",it->pid,pids.APIDs[i].desc,pids.APIDs[i].is_ac3 ? " (AC3)": " ");
			i++;
		}
	}

	if(pids.APIDs.empty())
		hh->printf("0\n"); // shouldnt happen, but print at least one apid
	if(pids.PIDs.vtxtpid)
		hh->printf("%05u vtxt\n",pids.PIDs.vtxtpid);
	if (pids.PIDs.pmtpid)
		hh->printf("%05u pmt\n",pids.PIDs.pmtpid);

}
//-----------------------------------------------------------------------------
void CControlAPI::SendTimers(CyhookHandler *hh)
{
	CTimerd::TimerList timerlist;			// List of bouquets
	bool send_id = false;

	if (hh->ParamList["format"] == "id")
		send_id = true;

	timerlist.clear();
	NeutrinoAPI->Timerd->getTimerList(timerlist);

	CTimerd::TimerList::iterator timer = timerlist.begin();

	for(; timer != timerlist.end(); timer++)
	{
		// Add Data
		char zAddData[22+1] = { 0 };
		if (send_id)
		{
			zAddData[0] = '0';
			zAddData[1] = 0;
		}

		switch(timer->eventType) {
		case CTimerd::TIMER_NEXTPROGRAM:
		case CTimerd::TIMER_ZAPTO:
		case CTimerd::TIMER_RECORD:
			if (!send_id)
			{
				strncpy(zAddData, NeutrinoAPI->GetServiceName(timer->channel_id).c_str(), 22);
				if (zAddData[0] == 0)
					strcpy(zAddData, NeutrinoAPI->Zapit->isChannelTVChannel(timer->channel_id) ? "Unknown TV-Channel" : "Unknown Radio-Channel");
			}
			else
				sprintf(zAddData, PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS, timer->channel_id);

			zAddData[22]=0;

			break;

		case CTimerd::TIMER_STANDBY:
			if (!send_id)
				sprintf(zAddData,"Standby: %s",(timer->standby_on ? "ON" : "OFF"));
			break;

		case CTimerd::TIMER_REMIND :
			if (!send_id)
				strncpy(zAddData, timer->message, 22);
			zAddData[22]=0;
			break;

		default:
			break;
		}

		hh->printf("%d %d %d %d %d %d %d %s\n",
			   timer->eventID,
			   (int)timer->eventType,
			   (int)timer->eventRepeat,
			   (int)timer->repeatCount,
			   (int)timer->announceTime,
			   (int)timer->alarmTime,
			   (int)timer->stopTime,
			   zAddData);
	}
}

//-----------------------------------------------------------------------------
void CControlAPI::_SendTime(CyhookHandler *hh, struct tm *Time, int digits) {
	char zTime[25] = {0};
	char zDate[25] = {0};
	strftime(zTime,20,"%H:%M",Time);
	strftime(zDate,20,"%d.%m.%Y",Time);
	hh->printf("\t\t\t\t\t<text>%s %s</text>\n",zDate,zTime);
	hh->printf("\t\t\t\t\t<date>%s</date>\n",zDate);
	hh->printf("\t\t\t\t\t<time>%s</time>\n",zTime);
	hh->printf("\t\t\t\t\t<digits>%d</digits>\n",digits);
	hh->printf("\t\t\t\t\t<day>%d</day>\n",Time->tm_mday);
	hh->printf("\t\t\t\t\t<month>%d</month>\n",Time->tm_mon+1);
	hh->printf("\t\t\t\t\t<year>%d</year>\n",Time->tm_year+1900);
	hh->printf("\t\t\t\t\t<hour>%d</hour>\n",Time->tm_hour);
	hh->printf("\t\t\t\t\t<min>%d</min>\n",Time->tm_min);
}
//-----------------------------------------------------------------------------
// build xml for all timer data (needed for yWeb 3)
//-----------------------------------------------------------------------------
void CControlAPI::SendTimersXML(CyhookHandler *hh)
{
	// Init local timer iterator
	CTimerd::TimerList timerlist;			// List of timers
	timerlist.clear();
	NeutrinoAPI->Timerd->getTimerList(timerlist);
	sort(timerlist.begin(), timerlist.end());		// sort timer
	CTimerd::TimerList::iterator timer = timerlist.begin();

	std::string xml_response = "";
	hh->SetHeader(HTTP_OK, "text/xml; charset=UTF-8");
	hh->WriteLn("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	hh->WriteLn("<timer>\n");

	// general timer configuration
	hh->WriteLn("\t<config>\n");

	// Look for Recording Safety Timers too
	int pre=0, post=0;
	NeutrinoAPI->Timerd->getRecordingSafety(pre,post);
//	hh->printf("\t\t\t<recording_safety>%d</recording_safety>\n",(int)timer->recordingSafety);
	hh->printf("\t\t\t<pre_delay>%d</pre_delay>\n",pre);
	hh->printf("\t\t\t<post_delay>%d</post_delay>\n",post);
	hh->WriteLn("\t</config>\n");

	// start timer list
	hh->WriteLn("\t<timer_list>\n");

	for(; timer != timerlist.end(); timer++)
	{
		hh->WriteLn("\t\t<timer>\n");
		hh->printf("\t\t\t<type>%s</type>\n",(NeutrinoAPI->timerEventType2Str(timer->eventType)).c_str());
		hh->printf("\t\t\t<id>%d</id>\n",timer->eventID);
		hh->printf("\t\t\t<state>%d</state>\n",(int)timer->eventState);
		hh->printf("\t\t\t<type_number>%d</type_number>\n",(int)timer->eventType);

		// alarmtime
		hh->WriteLn("\t\t\t<alarm>\n");

		struct tm *alarmTime = localtime(&(timer->alarmTime));
		hh->WriteLn("\t\t\t\t<normal>\n");
		_SendTime(hh, alarmTime, (int)timer->alarmTime);
		hh->WriteLn("\t\t\t\t</normal>\n");

		time_t real_alarmTimeT = timer->alarmTime - pre;
		struct tm *safetyAlarmTime = localtime(&real_alarmTimeT);
		hh->WriteLn("\t\t\t\t<safety>\n");
		_SendTime(hh, safetyAlarmTime, (int)real_alarmTimeT);
		hh->WriteLn("\t\t\t\t</safety>\n");

		hh->WriteLn("\t\t\t</alarm>\n");

		// announcetime
		hh->WriteLn("\t\t\t<announce>\n");
		struct tm *announceTime = localtime(&(timer->announceTime));
		hh->WriteLn("\t\t\t\t<normal>\n");
		_SendTime(hh, announceTime, (int)timer->announceTime);
		hh->WriteLn("\t\t\t\t</normal>\n");

		time_t real_announceTimeT = timer->announceTime - pre;
		struct tm *safetyAnnounceTime = localtime(&real_announceTimeT);
		hh->WriteLn("\t\t\t\t<safety>\n");
		_SendTime(hh, safetyAnnounceTime, (int)real_announceTimeT);
		hh->WriteLn("\t\t\t\t</safety>\n");

		hh->WriteLn("\t\t\t</announce>\n");

		// stoptime
		if(timer->stopTime > 0) {
			hh->WriteLn("\t\t\t<stop>\n");
			struct tm *stopTime = localtime(&(timer->stopTime));
			hh->WriteLn("\t\t\t\t<normal>\n");
			_SendTime(hh, stopTime, (int)timer->stopTime);
			hh->WriteLn("\t\t\t\t</normal>\n");

			time_t real_stopTimeT = timer->stopTime - post;
			struct tm *safetyStopTime = localtime(&real_stopTimeT);
			hh->WriteLn("\t\t\t\t<safety>\n");
			_SendTime(hh, safetyStopTime, (int)real_stopTimeT);
			hh->WriteLn("\t\t\t\t</safety>\n");

			hh->WriteLn("\t\t\t</stop>\n");
		}

		// repeat
		std::string zRep = NeutrinoAPI->timerEventRepeat2Str(timer->eventRepeat);
		std::string zRepCount;
		if (timer->eventRepeat == CTimerd::TIMERREPEAT_ONCE)
			zRepCount = "-";
		else
			zRepCount = (timer->repeatCount == 0) ? "&#x221E;" : string_printf("%dx",timer->repeatCount);
		hh->WriteLn("\t\t\t<repeat>\n");
		hh->printf("\t\t\t\t<count>%s</count>\n",zRepCount.c_str());
		hh->printf("\t\t\t\t<number>%d</number>\n",(int)timer->eventRepeat);
		hh->printf("\t\t\t\t<text>%s</text>\n",zRep.c_str());
		char weekdays[8]= {0};
		NeutrinoAPI->Timerd->setWeekdaysToStr(timer->eventRepeat, weekdays);
		hh->printf("\t\t\t\t<weekdays>%s</weekdays>\n",weekdays);
		hh->WriteLn("\t\t\t</repeat>\n");

		// channel infos
		std::string channel_name = NeutrinoAPI->GetServiceName(timer->channel_id);
		if (channel_name.empty())
			channel_name = NeutrinoAPI->Zapit->isChannelTVChannel(timer->channel_id) ? "Unknown TV-Channel" : "Unknown Radio-Channel";

		// epg title
		std::string title = timer->epgTitle;
		if(timer->epgID!=0) {
			CSectionsdClient sdc;
			CEPGData epgdata;
			if (sdc.getEPGid(timer->epgID, timer->epg_starttime, &epgdata))
				title = epgdata.title;
		}

		// timer specific data
		switch(timer->eventType)
		{
		case CTimerd::TIMER_NEXTPROGRAM : {
			hh->printf("\t\t\t<channel_id>" PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS "</channel_id>\n",timer->channel_id);
			hh->printf("\t\t\t<channel_name>%s</channel_name>\n",channel_name.c_str());
			hh->printf("\t\t\t<title>%s</title>\n",title.c_str());
		}
		break;

		case CTimerd::TIMER_ZAPTO : {
			hh->printf("\t\t\t<channel_id>" PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS "</channel_id>\n",timer->channel_id);
			hh->printf("\t\t\t<channel_name>%s</channel_name>\n",channel_name.c_str());
			hh->printf("\t\t\t<title>%s</title>\n",title.c_str());
		}
		break;

		case CTimerd::TIMER_RECORD : {
			hh->printf("\t\t\t<channel_id>" PRINTF_CHANNEL_ID_TYPE_NO_LEADING_ZEROS "</channel_id>\n",timer->channel_id);
			hh->printf("\t\t\t<channel_name>%s</channel_name>\n",channel_name.c_str());
			hh->printf("\t\t\t<title>%s</title>\n",title.c_str());

			// audio
			if(timer->apids != TIMERD_APIDS_CONF) {
				hh->WriteLn("\t\t\t<audio>\n");
				hh->WriteLn("\t\t\t\t<apids_conf>false</apids_conf>\n");
				if(timer->apids & TIMERD_APIDS_STD)
					hh->WriteLn("\t\t\t\t<apids_std>true</apids_std>\n");
				else
					hh->WriteLn("\t\t\t\t<apids_std>false</apids_std>\n");
				if(timer->apids & TIMERD_APIDS_ALT)
					hh->WriteLn("\t\t\t\t<apids_alt>true</apids_alt>\n");
				else
					hh->WriteLn("\t\t\t\t<apids_alt>false</apids_alt>\n");
				if(timer->apids & TIMERD_APIDS_AC3)
					hh->WriteLn("\t\t\t\t<apids_ac3>true</apids_ac3>\n");
				else
					hh->WriteLn("\t\t\t\t<apids_ac3>false</apids_ac3>\n");
				hh->WriteLn("\t\t\t</audio>\n");
			}
			else {
				hh->WriteLn("\t\t\t<audio>\n");
				hh->WriteLn("\t\t\t\t<apids_conf>true</apids_conf>\n");
				hh->WriteLn("\t\t\t\t<apids_std>false</apids_std>\n");
				hh->WriteLn("\t\t\t\t<apids_alt>false</apids_alt>\n");
				hh->WriteLn("\t\t\t\t<apids_ac3>false</apids_ac3>\n");
				hh->WriteLn("\t\t\t</audio>\n");
			}

			hh->printf("\t\t\t<recording_dir>%s</recording_dir>\n",timer->recordingDir);
			hh->printf("\t\t\t<epg_id>%d</epg_id>\n",(int)timer->epgID);

		}
		break;

		case CTimerd::TIMER_STANDBY : {
			hh->printf("\t\t\t<status>%s</status>\n",(timer->standby_on)? "on" : "off");
		}
		break;

		case CTimerd::TIMER_REMIND : {
			std::string _message;
			_message = std::string(timer->message).substr(0,20);
			hh->printf("\t\t\t<message>%s</message>\n",_message.c_str());
		}
		break;

		case CTimerd::TIMER_EXEC_PLUGIN : {
			hh->printf("\t\t\t<plugin>%s</plugin>\n",timer->pluginName);
		}
		break;

		case CTimerd::TIMER_SLEEPTIMER : {
		}
		break;

		case CTimerd::TIMER_IMMEDIATE_RECORD : {
		}
		break;

		default:
		{}
		}
		hh->WriteLn("\t\t</timer>\n");
	}
	hh->WriteLn("\t</timer_list>\n");
	hh->WriteLn("</timer>\n");
}

//-----------------------------------------------------------------------------
// yweb : Extentions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Dispatcher
//-----------------------------------------------------------------------------
void CControlAPI::YWebCGI(CyhookHandler *hh)
{
	bool status=true;
	int para;
	if (hh->ParamList["video_stream_pids"] != "")
	{
		para=0;
		sscanf( hh->ParamList["video_stream_pids"].c_str(), "%d", &para);
		YWeb_SendVideoStreamingPids(hh, para);
	}
	else if (hh->ParamList["1"] == "radio_stream_pid")
		YWeb_SendRadioStreamingPid(hh);

	if(!status)
		hh->SendError();
}

//-----------------------------------------------------------------------------
// Get Streaming Pids 0x$pmt,0x$vpid,0x$apid with apid_no is the Number of Audio-Pid
//-----------------------------------------------------------------------------
void CControlAPI::YWeb_SendVideoStreamingPids(CyhookHandler *hh, int apid_no)
{
	CZapitClient::responseGetPIDs pids;
	int apid=0,apid_idx=0;
	pids.PIDs.vpid=0;
	NeutrinoAPI->Zapit->getPIDS(pids);

	if( apid_no < (int)pids.APIDs.size())
		apid_idx=apid_no;
	if(!pids.APIDs.empty())
		apid = pids.APIDs[apid_idx].pid;
	if(hh->ParamList["no_commas"] != "")
		hh->printf("0x%04x 0x%04x 0x%04x",pids.PIDs.pmtpid,pids.PIDs.vpid,apid);
	else
		hh->printf("0x%04x,0x%04x,0x%04x",pids.PIDs.pmtpid,pids.PIDs.vpid,apid);
}

//-----------------------------------------------------------------------------
// Get Streaming Pids 0x$pmt,0x$vpid,0x$apid with apid_no is the Number of Audio-Pid
//-----------------------------------------------------------------------------
void CControlAPI::YWeb_SendRadioStreamingPid(CyhookHandler *hh)
{
	CZapitClient::responseGetPIDs pids;
	int apid=0;
	NeutrinoAPI->Zapit->getPIDS(pids);

	if(!pids.APIDs.empty())
		apid = pids.APIDs[0].pid;
	hh->printf("0x%04x",apid);
}

//-----------------------------------------------------------------------------
std::string CControlAPI::YexecuteScript(CyhookHandler *, std::string cmd)
{
	std::string script, para, result;
	bool found = false;

	// split script and parameters
	int pos;
	if ((pos = cmd.find_first_of(" ")) > 0)
	{
		script = cmd.substr(0, pos);
		para = cmd.substr(pos+1,cmd.length() - (pos+1)); // snip
	}
	else
		script=cmd;
	// get file
	std::string fullfilename;
	script += ".sh"; //add script extention
	char cwd[255];
	getcwd(cwd, 254);

	for (unsigned int i=0; i<PLUGIN_DIR_COUNT && !found; i++)
	{
		fullfilename = PLUGIN_DIRS[i]+"/"+script;
		FILE *test =fopen(fullfilename.c_str(),"r"); // use fopen: popen does not work
		if( test != NULL )
		{
			fclose(test);
			chdir(PLUGIN_DIRS[i].c_str());
			FILE *f = popen( (fullfilename+" "+para).c_str(),"r"); //execute
			if (f != NULL)
			{
				found = true;

				char output[1024];
				while (fgets(output,1024,f)) // get script output
					result += output;
				pclose(f);
			}
		}
	}
	chdir(cwd);

	if (!found)
	{
		printf("[CControlAPI] script %s not found in\n",script.c_str());
		for (unsigned int i=0; i<PLUGIN_DIR_COUNT; i++) {
			printf("%s\n",PLUGIN_DIRS[i].c_str());
		}
		result="error";
	}
	return result;
}
//-------------------------------------------------------------------------
void CControlAPI::doModifyTimer(CyhookHandler *hh)
{
	hh->ParamList["update"]="1";
	doNewTimer(hh);
}
//-------------------------------------------------------------------------
void CControlAPI::doNewTimer(CyhookHandler *hh)
{
	time_t	announceTimeT = 0,
		stopTimeT = 0,
		alarmTimeT = 0,
		tnull = 0;
	unsigned int repCount = 0;
	int alHour=0;

	// if alarm given then in parameters im time_t format
	if(hh->ParamList["alarm"] != "")
	{
		alarmTimeT = atoi(hh->ParamList["alarm"].c_str());
		if(hh->ParamList["stop"] != "")
			stopTimeT = atoi(hh->ParamList["stop"].c_str());
		if(hh->ParamList["announce"] != "")
			announceTimeT = atoi(hh->ParamList["announce"].c_str());
		else
			announceTimeT = alarmTimeT;
	}
	else if(hh->ParamList["alDate"] != "") //given formatted
	{
		// Alarm Date - Format exact! DD.MM.YYYY
		tnull = time(NULL);
		struct tm *alarmTime=localtime(&tnull);
		alarmTime->tm_sec = 0;
		strptime(hh->ParamList["alDate"].c_str(), "%d.%m.%Y", alarmTime);

		// Alarm Time - Format exact! HH:MM
		if(hh->ParamList["alTime"] != "")
			strptime(hh->ParamList["alTime"].c_str(), "%H:%M", alarmTime);
		alHour = alarmTime->tm_hour;

		correctTime(alarmTime);
		alarmTimeT = mktime(alarmTime);
		announceTimeT = alarmTimeT;
		struct tm *stopTime = localtime(&alarmTimeT);
		stopTime->tm_sec = 0;
		// Stop Time - Format exact! HH:MM
		if(hh->ParamList["stTime"] != "")
			strptime(hh->ParamList["stTime"].c_str(), "%H:%M", stopTime);

		// Stop Date - Format exact! DD.MM.YYYY
		if(hh->ParamList["stDate"] != "")
			strptime(hh->ParamList["stDate"].c_str(), "%d.%m.%Y", stopTime);
		stopTime->tm_sec = 0;
		correctTime(stopTime);
		stopTimeT = mktime(stopTime);
		if(hh->ParamList["stDate"] == "" && alHour > stopTime->tm_hour)
			stopTimeT += 24* 60 * 60; // add 1 Day
	}
	else	// alarm/stop time given in pieces
	{
		// alarm time
		time_t now = time(NULL);
		struct tm *alarmTime=localtime(&now);
		if(hh->ParamList["ad"] != "")
			alarmTime->tm_mday = atoi(hh->ParamList["ad"].c_str());
		if(hh->ParamList["amo"] != "")
			alarmTime->tm_mon = atoi(hh->ParamList["amo"].c_str())-1;
		if(hh->ParamList["ay"] != "")
			alarmTime->tm_year = atoi(hh->ParamList["ay"].c_str())-1900;
		if(hh->ParamList["ah"] != "")
			alarmTime->tm_hour = atoi(hh->ParamList["ah"].c_str());
		if(hh->ParamList["ami"] != "")
			alarmTime->tm_min = atoi(hh->ParamList["ami"].c_str());
		alarmTime->tm_sec = 0;
		correctTime(alarmTime);
		alarmTimeT = mktime(alarmTime);
		announceTimeT = alarmTimeT;

		// stop time
		struct tm *stopTime = alarmTime;
		if(hh->ParamList["sd"] != "")
			stopTime->tm_mday = atoi(hh->ParamList["sd"].c_str());
		if(hh->ParamList["smo"] != "")
			stopTime->tm_mon = atoi(hh->ParamList["smo"].c_str())-1;
		if(hh->ParamList["sy"] != "")
			stopTime->tm_year = atoi(hh->ParamList["sy"].c_str())-1900;
		if(hh->ParamList["sh"] != "")
			stopTime->tm_hour = atoi(hh->ParamList["sh"].c_str());
		if(hh->ParamList["smi"] != "")
			stopTime->tm_min = atoi(hh->ParamList["smi"].c_str());
		stopTime->tm_sec = 0;
		correctTime(stopTime);
		stopTimeT = mktime(stopTime);
	}

	if(announceTimeT != 0)
		announceTimeT -= 60;

	CTimerd::CTimerEventTypes type;
	if(hh->ParamList["type"] != "")
		type  = (CTimerd::CTimerEventTypes) atoi(hh->ParamList["type"].c_str());
	else // default is: record
		type = CTimerd::TIMER_RECORD;

	// repeat
	if(hh->ParamList["repcount"] != "")
	{
		repCount = atoi(hh->ParamList["repcount"].c_str());
	}
	CTimerd::CTimerEventRepeat rep;
	if(hh->ParamList["rep"] != "")
		rep = (CTimerd::CTimerEventRepeat) atoi(hh->ParamList["rep"].c_str());
	else // default: no repeat
		rep = (CTimerd::CTimerEventRepeat)0;

	if(((int)rep) >= ((int)CTimerd::TIMERREPEAT_WEEKDAYS) && hh->ParamList["wd"] != "")
		NeutrinoAPI->Timerd->getWeekdaysFromStr(&rep, hh->ParamList["wd"].c_str());
	// apids
	bool changeApids=false;
	unsigned char apids=0;
	if(hh->ParamList["apcf"] == "on")
	{
		changeApids=true;
		apids=0;
	}
	else
	{
		if(hh->ParamList["apst"] == "on")
		{
			changeApids=true;
			apids |= TIMERD_APIDS_STD;
		}
		if(hh->ParamList["apal"] == "on")
		{
			changeApids=true;
			apids |= TIMERD_APIDS_ALT;
		}
		if(hh->ParamList["apac"] == "on")
		{
			changeApids=true;
			apids |= TIMERD_APIDS_AC3;
		}
	}
	CTimerd::RecordingInfo recinfo;
	CTimerd::EventInfo eventinfo;
	eventinfo.epgID = 0;
	eventinfo.epg_starttime = 0;
	eventinfo.apids = TIMERD_APIDS_CONF;
	eventinfo.recordingSafety = (hh->ParamList["rs"] == "1");

	// channel by Id or name
	if(hh->ParamList["channel_id"] != "")
		sscanf(hh->ParamList["channel_id"].c_str(),
		       SCANF_CHANNEL_ID_TYPE,
		       &eventinfo.channel_id);
	else
		eventinfo.channel_id = NeutrinoAPI->ChannelNameToChannelId(hh->ParamList["channel_name"]);

	std::string _rec_dir = hh->ParamList["rec_dir"];
	void *data=NULL;
	if(type == CTimerd::TIMER_RECORD)
		announceTimeT-=120;
	if(type == CTimerd::TIMER_STANDBY)
	{
		bool standby_on = (hh->ParamList["sbon"]=="1");
		data=&standby_on;
	}
	else if(type==CTimerd::TIMER_NEXTPROGRAM || type==CTimerd::TIMER_ZAPTO)
		data= &eventinfo;
	else if (type==CTimerd::TIMER_RECORD)
	{
		if(_rec_dir == "")
		{
			// get Default Recordingdir
			CConfigFile *Config = new CConfigFile(',');
			Config->loadConfig(NEUTRINO_CONFIGFILE);
			_rec_dir = Config->getString("network_nfs_recordingdir", "/mnt/filme");
			delete Config;//Memory leak: Config
		}
		if(changeApids)
			eventinfo.apids = apids;
		recinfo = eventinfo;
		strncpy(recinfo.recordingDir, _rec_dir.c_str(), RECORD_DIR_MAXLEN-1);
		data = &recinfo;
	}
	else if(type==CTimerd::TIMER_REMIND)
	{
		char msg[REMINDER_MESSAGE_MAXLEN];
		memset(msg, 0, sizeof(msg));
		strncpy(msg, hh->ParamList["msg"].c_str(),REMINDER_MESSAGE_MAXLEN-1);
		data=msg;
	}
	else if(type==CTimerd::TIMER_EXEC_PLUGIN)
	{
		char msg[EXEC_PLUGIN_NAME_MAXLEN];
		memset(msg, 0, sizeof(msg));
		strncpy(msg, hh->ParamList["PluginName"].c_str(),EXEC_PLUGIN_NAME_MAXLEN-1);
		data=msg;
	}
	// update or add timer
	if(hh->ParamList["update"]=="1")
	{
		if(hh->ParamList["id"] != "")
		{
			unsigned modyId = atoi(hh->ParamList["id"].c_str());
			NeutrinoAPI->Timerd->removeTimerEvent(modyId);
		}
		else
		{
			CTimerd::TimerList timerlist;
			timerlist.clear();
			NeutrinoAPI->Timerd->getTimerList(timerlist);
			CTimerd::TimerList::iterator timer = timerlist.begin();

			// Look for Recording Safety Timers too
			time_t real_alarmTimeT = alarmTimeT;
			if(eventinfo.recordingSafety)
			{
				int pre,post;
				NeutrinoAPI->Timerd->getRecordingSafety(pre,post);
				real_alarmTimeT -= pre;
			}

			for(; timer != timerlist.end(); timer++)
				if(timer->alarmTime == real_alarmTimeT)
				{
					NeutrinoAPI->Timerd->removeTimerEvent(timer->eventID);
					break;
				}
		}
	}
	NeutrinoAPI->Timerd->addTimerEvent(type,data,announceTimeT,alarmTimeT,stopTimeT,rep,repCount);
	hh->SendOk();
}
//-------------------------------------------------------------------------
void CControlAPI::setBouquetCGI(CyhookHandler *hh)
{
	if (hh->ParamList["selected"] != "") {
		int selected = atoi(hh->ParamList["selected"].c_str());
		if(hh->ParamList["action"].compare("hide") == 0)
			NeutrinoAPI->Zapit->setBouquetHidden(selected - 1,true);
		else if(hh->ParamList["action"].compare("show") == 0)
			NeutrinoAPI->Zapit->setBouquetHidden(selected - 1,false);
		else if(hh->ParamList["action"].compare("lock") == 0)
			NeutrinoAPI->Zapit->setBouquetLock(selected - 1,true);
		else if(hh->ParamList["action"].compare("unlock") == 0)
			NeutrinoAPI->Zapit->setBouquetLock(selected - 1,false);
		hh->SendOk();
	}
	else
		hh->SendError();
}
//-------------------------------------------------------------------------
void CControlAPI::saveBouquetCGI(CyhookHandler *hh)
{
	NeutrinoAPI->Zapit->saveBouquets();
	NeutrinoAPI->UpdateBouquets();
	hh->SendOk();
}
//-------------------------------------------------------------------------
void CControlAPI::moveBouquetCGI(CyhookHandler *hh)
{
	if (hh->ParamList["selected"] != "" && (
				hh->ParamList["action"] == "up" ||
				hh->ParamList["action"] == "down"))
	{
		int selected = atoi(hh->ParamList["selected"].c_str());
		if (hh->ParamList["action"] == "up") {
			NeutrinoAPI->Zapit->moveBouquet(selected - 1, (selected - 1) - 1);
			selected--;
		} else {
			NeutrinoAPI->Zapit->moveBouquet(selected - 1, (selected + 1) - 1);
			selected++;
		}
		hh->SendOk();
	}
	else
		hh->SendError();
}
//-------------------------------------------------------------------------
void CControlAPI::deleteBouquetCGI(CyhookHandler *hh)
{
	int selected = -1;

	if (hh->ParamList["selected"] != "") {
		selected = atoi(hh->ParamList["selected"].c_str());
		NeutrinoAPI->Zapit->deleteBouquet(selected - 1);
		hh->SendOk();
	}
	else
		hh->SendError();
}
//-------------------------------------------------------------------------
void CControlAPI::addBouquetCGI(CyhookHandler *hh)
{
	if (!hh->ParamList["name"].empty())
	{
		std::string tmp = hh->ParamList["name"];
		if (NeutrinoAPI->Zapit->existsBouquet(tmp.c_str()) == -1)
		{
			NeutrinoAPI->Zapit->addBouquet(tmp.c_str());
			hh->SendOk();
		}
		else
			hh->SendError();
	}
}
//-------------------------------------------------------------------------
void CControlAPI::renameBouquetCGI(CyhookHandler *hh)
{
	if (hh->ParamList["selected"] != "")
	{
		if (hh->ParamList["nameto"] != "")
		{
			if (NeutrinoAPI->Zapit->existsBouquet((hh->ParamList["nameto"]).c_str()) == -1)
			{
				NeutrinoAPI->Zapit->renameBouquet(atoi(hh->ParamList["selected"].c_str()) - 1, hh->ParamList["nameto"].c_str());
				hh->SendOk();
				return;
			}
		}
	}
	hh->SendError();
}
//-------------------------------------------------------------------------
void CControlAPI::changeBouquetCGI(CyhookHandler *hh)
{
	if (!(hh->ParamList["selected"].empty()))
	{
		int selected = atoi(hh->ParamList["selected"].c_str());
		CZapitClient::BouquetChannelList BChannelList;
		NeutrinoAPI->Zapit->getBouquetChannels(selected - 1, BChannelList, CZapitClient::MODE_CURRENT, true);
		CZapitClient::BouquetChannelList::iterator channels = BChannelList.begin();
		for(; channels != BChannelList.end(); channels++)
		{
			NeutrinoAPI->Zapit->removeChannelFromBouquet(selected - 1, channels->channel_id);
		}

		t_channel_id channel_id;
		int delta;
		const char * bchannels = hh->ParamList["bchannels"].c_str();
		while (sscanf(bchannels,
				SCANF_CHANNEL_ID_TYPE
				"%n",
				&channel_id,
				&delta) > 0)
		{
			NeutrinoAPI->Zapit->addChannelToBouquet(selected - 1, channel_id);
			bchannels += (delta + 1); // skip the separating ',', too
		}

		NeutrinoAPI->Zapit->renumChannellist();
		NeutrinoAPI->UpdateBouquets();
		if(hh->ParamList["redirect"] != "")
			hh->SendRewrite(hh->ParamList["redirect"]);
		else
			hh->SendOk();
	}
	else
		hh->SendError();
}
//-------------------------------------------------------------------------
void CControlAPI::updateBouquetCGI(CyhookHandler *hh)
{
	NeutrinoAPI->UpdateBouquets();
	hh->SendOk();
}
//-------------------------------------------------------------------------
// audio_no : (optional) audio channel
// host : (optional) ip of dbox
void CControlAPI::build_live_url(CyhookHandler *hh)
{
	std::string xpids,port,yresult;
	int mode = NeutrinoAPI->Zapit->getMode();

	if ( mode == CZapitClient::MODE_TV)
	{
		CZapitClient::responseGetPIDs pids;
		int apid=0,apid_no=0,apid_idx=0;
		pids.PIDs.vpid=0;

		if(hh->ParamList["audio_no"] !="")
			apid_no = atoi(hh->ParamList["audio_no"].c_str());
		NeutrinoAPI->Zapit->getPIDS(pids);

		if( apid_no < (int)pids.APIDs.size())
			apid_idx=apid_no;
		if(!pids.APIDs.empty())
			apid = pids.APIDs[apid_idx].pid;
		xpids = string_printf("0x%04x,0x%04x,0x%04x",pids.PIDs.pmtpid,pids.PIDs.vpid,apid);
	}
	else if ( mode == CZapitClient::MODE_RADIO)
	{
		CZapitClient::responseGetPIDs pids;
		int apid=0;

		NeutrinoAPI->Zapit->getPIDS(pids);
		if(!pids.APIDs.empty())
			apid = pids.APIDs[0].pid;

		//xpids = string_printf("0x%04x",apid);
		xpids = string_printf("0x%04x,0x%04x",pids.PIDs.pmtpid,apid);
	}
	else
		hh->SendError();
	// build url
	std::string url = "";
	if(hh->ParamList["host"] !="")
		url = "http://"+hh->ParamList["host"];
	else
		url = "http://"+hh->HeaderList["Host"];
	//url += (mode == CZapitClient::MODE_TV) ? ":31339/0," : ":31338/";
	url += ":31339/0,";
	url += xpids;

	// response url
	if(hh->ParamList["vlc_link"] !="")
	{
		write_to_file("/tmp/vlc.m3u", url);
		hh->SendRedirect("/tmp/vlc.m3u");
	}
	else
	{
		hh->SetHeader(HTTP_OK, "text/html; charset=UTF-8");
		hh->Write(url);
	}
}
//-------------------------------------------------------------------------
void CControlAPI::logoCGI(CyhookHandler *hh)
{
	t_channel_id channel_id;
	sscanf(hh->ParamList["1"].c_str(),
	       SCANF_CHANNEL_ID_TYPE,
	       &channel_id);
	hh->Write(NeutrinoAPI->getLogoFile(hh->WebserverConfigList["Tuxbox.LogosURL"], channel_id));
}
//-------------------------------------------------------------------------
void CControlAPI::ConfigCGI(CyhookHandler *hh)
{
	bool load = true;
	CConfigFile *Config = new CConfigFile(',');
	ConfigDataMap conf;
	std::string config_filename="";
	std::string error = "";
	std::string result = "";

	if (hh->ParamList["action"] == "submit")
		load = false;

	// Para "config" describes the config type
	if(hh->ParamList["config"] == "neutrino")
		config_filename = NEUTRINO_CONFIGFILE;
	else if(hh->ParamList["config"] == "nhttpd")
		config_filename = HTTPD_CONFIGFILE;
	else if(hh->ParamList["config"] == "yweb")
		config_filename = YWEB_CONFIGFILE;

	if(config_filename != "") {
		Config->loadConfig(config_filename);

		if(load) {
			conf = Config->getConfigDataMap();
			ConfigDataMap::iterator it;
			for(it = conf.begin(); it != conf.end(); it++) {
				std::string key =it->first;
				replace(key,".","_dot_");
				replace(key,"-","_bind_");
				if(!(hh->ParamList["config"] == "nhttpd" && it->first == "mod_auth.password")) {
					// Output as json (default)
					if (hh->ParamList["format"] == "json" || hh->ParamList["format"] == "") {
						result += string_printf("%s: '%s',\n", (key).c_str(), (it->second).c_str());
					}
				}
			}
		} else {
			for (CStringList::iterator it = hh->ParamList.begin(); it
					!= hh->ParamList.end(); it++) {
				std::string key = it->first;
				replace(key,"_dot_",".");
				replace(key,"_bind_","-");
				if(key != "_dc" && key != "action" && key != "format" && key != "config") {
					Config->setString(key, it->second);
				}
			}
			if(config_filename != "")
				Config->saveConfig(config_filename);
		}
	}
	else
		error = string_printf("no config defined for: %s", (hh->ParamList["config"]).c_str() );

	if(error == "") {
		if (hh->ParamList["format"] == "json" || hh->ParamList["format"] == "") {
			hh->WriteLn("{success: 'true', data:{");
			hh->WriteLn(result);
			hh->WriteLn("}}");
		}
	} else {
		if (hh->ParamList["format"] == "json" || hh->ParamList["format"] == "") {
			hh->WriteLn("{success: 'false', error:{");
			hh->WriteLn(error);
			hh->WriteLn("}}");
		}
	}

	delete Config;
}
//-----------------------------------------------------------------------------
// File handling
// action=list|new_folder|delete|read_file|write_file|set_properties
// path=<path>
// TODO: development in progress!
//-----------------------------------------------------------------------------
void CControlAPI::FileCGI(CyhookHandler *hh)
{

	// directory list: action=list&path=<path>
	if (hh->ParamList["action"] == "list") {
		DIR *dirp;
		struct dirent *entry;
		struct stat statbuf;
		struct passwd  *pwd;
		struct group   *grp;
		struct tm      *tm;
		char            datestring[256];

		hh->SetHeader(HTTP_OK, "text/xml; charset=UTF-8");
		hh->WriteLn("<filelist>");
		std::string path = hh->ParamList["path"];
		if((dirp = opendir( path.c_str() )))
		{
			while((entry = readdir(dirp))) {
				hh->WriteLn("\t<item>");
				hh->printf("\t\t<name>%s</name>\n", entry->d_name);
				std::string ftype;
				if(entry->d_type == DT_DIR)
					ftype="dir";
				else if(entry->d_type == DT_LNK)
					ftype="lnk";
				else if(entry->d_type == 8)
					ftype="file";

				hh->printf("\t\t<type_str>%s</type_str>", ftype.c_str() );
				hh->printf("\t\t<type>%d</type>", (int)entry->d_type);
				if(path[path.length() - 1] != '/')
					path+="/";
				std::string fullname = path + entry->d_name;
				hh->printf("\t\t<fullname>%s</fullname>", fullname.c_str());
//entry->d_name
				if(stat(fullname.c_str(), &statbuf)!=-1) {
					hh->printf("\t\t<mode>%xld</mode>", (long)statbuf.st_mode);
					/* Print out type, permissions, and number of links. */
//					hh->printf("\t\t<permission>%10.10s</permission>\n", sperm (statbuf.st_mode));
					hh->printf("\t\t<nlink>%d</nlink>\n", statbuf.st_nlink);
					/* Print out owner's name if it is found using getpwuid(). */
					if ((pwd = getpwuid(statbuf.st_uid)) != NULL)
						hh->printf("\t\t<user>%s</user>\n", pwd->pw_name);
					else
						hh->printf("\t\t<user>%d</user>\n", statbuf.st_uid);
					/* Print out group name if it is found using getgrgid(). */
					if ((grp = getgrgid(statbuf.st_gid)) != NULL)
						hh->printf("\t\t<group>%s</group>\n", grp->gr_name);
					else
						hh->printf("\t\t<group>%d</group>\n", statbuf.st_gid);
					/* Print size of file. */
					hh->printf("\t\t<size>%jd</size>\n", (intmax_t)statbuf.st_size);
					tm = localtime(&statbuf.st_mtime);
					/* Get localized date string. */
					strftime(datestring, sizeof(datestring), nl_langinfo(D_T_FMT), tm);
					hh->printf("\t\t<time>%s</time>\n", datestring);
					hh->printf("\t\t<time_t>%ld</time_t>\n", (long)statbuf.st_mtime);
				}
				hh->WriteLn("\t</item>");
			}
			closedir(dirp);
		}
		hh->WriteLn("</filelist>");
	}
	// create new folder
	else if (hh->ParamList["action"] == "new_folder") {
		hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
		//TODO
	}
	else if (hh->ParamList["action"] == "delete") {
		hh->SetHeader(HTTP_OK, "text/plain; charset=UTF-8");
		//TODO
	}
}
