// LIDAR Toolkit
// Bob McGaughey, USDA Forest Service
//
// header file should be added in the normal location in the module containing _tmain()
// #include "command_line_core_functions.h"
//
// code should be included at end of module containing _tmain() using the following:
// #include "command_line_core_functions.cpp"
//
// global functions that are NOT TYPICALLY modified to create a new program
//
// Jan 2007  added use of environment variable LTKLOG to provide option to use a specific log file
//           This should be easier to use in batch programs that do lots of processing as you can
//           simply set the variable at the beginning of the batch file and clear it at the end.
//
// 11/10/2010 toolkit
//	Added ability to check for bad command line switches using the LTKCL_VerifyCommandLine() function. Each program defines
//	a string containing all of the valid switch names and then calls LTKCL_VerifyCommandLine(). If the command line contains
//	a switch that is not in the list an error is reported.
//
// 8/20/2013 toolkit
//	Added NOLASZIPDLL environment variable and /nolaszipdll switch to control use of LASzip DLL (c) Martin Isenburg for reading
//	LAS and LAZ files. By default all programs the read point data will use the DLL if it is present in the FUSION install folder.
//	The environment variable and command line switch allow you to disable the use of the DLL and rely on older FUSION code for reading
//	LAS files. When you disable the DLL, you also remove support for LAZ (compressed LAS) files.
//
// 5/21/2014 toolkit
//	Modified the behavior for verbose status to omit the elapsed time when not outputing a line terminator. This cleans up the status
//	when you want a message followed by a completion indicator on the same line. e.g. Doing something...Done!
//
// 3/1/2022 toolkit
//	Added ability to send verbose messages to log file. Use /verbose:100 to activate. This will also actvate all messages so you only
//	want to use this when needed (usually for debugging).

#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>

BOOL LTKCL_VerifyCommandLine(const char* ValidSwitches)
{
	BOOL retcode = FALSE;
	CString csTemp;
	CString tsSwitch;
	CString tsValidSwitches(ValidSwitches);
	CString BaseSwitches(" quiet interactive newlog log version verbose locale nolaszipdll skipfilecheck ");

	// add spaces to the beginning and end of the valid switch string
	tsValidSwitches.Insert(0, " ");
	tsValidSwitches.Insert(tsValidSwitches.GetLength() + 1, " ");

	// make lower case
	tsValidSwitches.MakeLower();

	// step through command line switches and make sure they are recognized
	for (int i = 1; i <= m_clp.SwitchCount(); i ++) {
		// get next switch
		tsSwitch = m_clp.ParamStr(i);

		// make lower case
		tsSwitch.MakeLower();

		// get rid of leading "/"
		tsSwitch.Replace("/", "");

		// trim before ":"
		if (tsSwitch.Find(":") >= 0)
			tsSwitch = tsSwitch.Left(tsSwitch.Find(":"));

		// add a space to the beginning and end of the switch string...helps when a keyword is also embedded in another keyword
		// e.g. lasclass and class
		tsSwitch = _T(" ") + tsSwitch + _T(" ");

		// make sure it is valid for this program
		if (tsValidSwitches.Find(tsSwitch) < 0) {
			if (BaseSwitches.Find(tsSwitch) < 0) {
				csTemp.Format("   Invalid command line switch:%s", (LPCTSTR) tsSwitch);
				LTKCL_PrintStatus(csTemp);

				m_nRetCode = 999;
				retcode = TRUE;
			}
		}
	}
	return(retcode);
}

void LTKCL_ParseStandardSwitches()
{
	m_RunQuiet = m_clp.Switch("quiet");
	m_RunInteractive = m_clp.Switch("interactive");
	m_ClearMasterLogFile = m_clp.Switch("newlog");
	m_ReportVersionInfo = m_clp.Switch("version");
	m_ShowVerboseStatus = m_clp.Switch("verbose");
	m_UseLocale = m_clp.Switch("locale");
	m_SkipFileCheck = m_clp.Switch("skipfilecheck");

	// check for LASzip dll option...we don't want the lack of the option to override the environment variable NOLASZIPDLL
	if (m_clp.Switch("nolaszipdll"))
		m_NoLASzip_DLL = TRUE;
		
	// look for /log:filename switch to use a specific log file...not cleared by default...must use /newlog to clear
	if (m_clp.Switch("log")) {
		CString LogFileName = m_clp.GetSwitchStr("log", "");
		if (!LogFileName.IsEmpty()) {
			m_MasterLogFileName = LogFileName;
			CFileSpec fs((LPCTSTR) LogFileName);
			if (fs.Extension().IsEmpty())
				fs.SetExt(".log");
			m_MasterLogFileName = fs.GetFullSpec();

			// do csv log file
			fs.SetExt(".csv");
			m_MasterCSVLogFileName = fs.GetFullSpec();
		}
	}

	// look for verbose level to control amount of info when /verbose is used
	m_VerboseLevel = 1;
	if (m_ShowVerboseStatus)
		m_VerboseLevel = m_clp.GetSwitchInt("verbose", 1);
}

void LTKCL_PrintStandardSwitchInfo(BOOL UsesPoints)
{
//			         1         2         3         4         5         6         7         8         9         10
//			1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
	printf("  Switches are preceeded by a \"/\". If a switch has multiple parameters after\n");
	printf("  the \":\", they should be separated by a single comma with no spaces before\n");
	printf("  or after the comma.\n\n");
	printf("  interactive Present a dialog-based interface\n");
	printf("  quiet       Suppress all output during the run\n");
	printf("  verbose     Display all status information during the run\n");
	printf("  version     Report version information and exit with no processing\n");
	printf("  newlog      Erase the existing log file and start a new log\n");
	printf("  log:name    Use the name specified for the log file\n");
	printf("  locale      Adjust program logic to input and output locale-specific numeric\n");
	printf("              formats (e.g. use a comma for the decimal separator)\n");
	if (UsesPoints) {
		printf("  nolaszipdll suppress the use of the LASzip dll (c) Martin Isenburg...\n");
		printf("              removes support for compressed LAS (LAZ) files. This option\n");
		printf("              is only useful for programs that read or write point files.\n");
		printf("  skipfilecheck Skip logic that checks for valid point files and removes\n");
		printf("              those that are invalid prior to the start of point\n");
		printf("              processing. This option is designed to overcome some\n");
		printf("              limitations with windows and wildcard processing.\n");
		printf("              This option is only useful for programs that read point\n");
		printf("              data.\n");
	}
}

BOOL LTKCL_Initialize()
{
	// initialize global variables
	m_MasterLogFileName.Empty();
	m_MasterCSVLogFileName.Empty();
	m_nRetCode = 0;
	m_RunQuiet = FALSE;
	m_ShowVerboseStatus = FALSE;
	m_RunInteractive = FALSE;
	m_ReportVersionInfo = FALSE;
	m_UseLocale = FALSE;
	m_SkipFileCheck = FALSE;
	m_NoLASzip_DLL = FALSE;

	// get starting time
	time(&m_StartTime);                 // Get time in seconds

	// set up name for master log file...get application path and add "LTKCL_master.log"
	CFileSpec fs(CFileSpec::FS_APPDIR);
	fs.SetFileNameEx("LTKCL_master.log");
	m_MasterLogFileName = fs.GetFullSpec();
	fs.SetFileNameEx("LTKCL_master.csv");
	m_MasterCSVLogFileName = fs.GetFullSpec();

	// look for environment variable LTKLOG to set the log file...assume it is the full path for the log file
	char* logenv = getenv("LTKLOG");
	if (logenv) {
		fs.SetFullSpec(logenv);

		// look for extension
		if (fs.Extension().IsEmpty())
			fs.SetExt(".log");

		m_MasterLogFileName = fs.GetFullSpec();

		// change to csv log
		fs.SetExt(".csv");
		m_MasterCSVLogFileName = fs.GetFullSpec();
	}

	// look for environment variable to suppress use of LASzip.dll
	char* LASzipdll = getenv("NOLASZIPDLL");
	if (LASzipdll) {
		m_NoLASzip_DLL = TRUE;
	}
	
	// parse standard switches...will override any environment variable related settings
	LTKCL_ParseStandardSwitches();
	
	// set locale-specific formatting for numeric data
	if (m_UseLocale)
		setlocale(LC_NUMERIC, "");

	LTKCL_PrintToLog("**********");

#if defined(_M_IX86)
	// look for FUSION64 environment variable telling us to use 64-bit FUSION programs if they exist
	char* prog64 = getenv("FUSION64");
	if (prog64) {
		// build the name for the 64-bit version and see if it exists
		CString progname = _T(PROGRAM_NAME);
		progname += "64.exe";
		CFileSpec progfs(CFileSpec::FS_APPDIR);
		progfs.SetFileNameEx((LPCTSTR) progname);
		if (progfs.Exists()) {
			// build the command line using the new program name and the current command line
			CString cmdline = progfs.GetFullSpec();
			cmdline += " ";
			for (int i = 1; i < m_clp.ParamCount(); i++) {
				cmdline += m_clp.ParamStr(i);
				cmdline += " ";
			}

			// report information...always write entry to log indicating transfer from 32-bit to 64-bit version...only output to screen with /verbose
			CString csTemp;
			csTemp.Format("%s v%.2f 32-bit transferring control to 64-bit version...", PROGRAM_NAME, PROGRAM_VERSION);
			csTemp += progfs.GetFileNameEx();
			LTKCL_PrintVerboseStatus((LPCTSTR) csTemp);
			LTKCL_PrintToLog((LPCTSTR) csTemp);

			// start 64-bit version
			system((LPCTSTR) cmdline);

			// exit 32-bit version
			exit(0);
		}
}
#endif

	// see if /version was used and report version info
	if (m_ReportVersionInfo) {
		CString csTemp;
#if defined(_M_X64)
		csTemp.Format("%s v%.2f 64-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#elif defined(_M_IX86)
		csTemp.Format("%s v%.2f 32-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#else
		csTemp.Format("%s v%.2f ??-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#endif
#ifdef _DEBUG
		csTemp += _T(" DEBUG");
#endif

#ifdef USE_LASLIB
		csTemp += _T(" (LASlib)");
#endif
		// prints the program header with the program name, version, build date
		LTKCL_PrintStatus((LPCTSTR) csTemp);

		LTKCL_PrintStatus(" --Robert J. McGaughey--USDA Forest Service--Pacific Northwest Research Station");

		return(FALSE);
	}

	return(TRUE);
}

void LTKCL_PrintToCSVLog(LPCTSTR lpszStatus)
{
	if (!m_MasterCSVLogFileName.IsEmpty()) {
		// see if log file exists...if not we need to add header
		int AddHeader = _access(m_MasterCSVLogFileName, 0);

		FILE* f = fopen(m_MasterCSVLogFileName, "at+");
		
		if (AddHeader)
			fprintf(f, "Program,Version,Program build date,Command line,Start time,Stop time,Elapsed time (seconds),Status\n");

		fprintf(f, "%s\n", lpszStatus);
		fclose(f);
	}
}

void LTKCL_PrintToLog(LPCTSTR lpszStatus, BOOL AddNewLine)
{
	// if log file output is disabled, don't do anything
	if (m_NoLogOutput)
		return;

	if (m_ClearMasterLogFile) {
		DeleteFile(m_MasterLogFileName);
		m_ClearMasterLogFile = FALSE;

		// delete csv log file and write new header line
		DeleteFile(m_MasterCSVLogFileName);
//		LTKCL_PrintToCSVLog("Program,Version,Program build date,Command line,Start time,Stop time,Elapsed time (seconds),Status");
	}

	if (!m_MasterLogFileName.IsEmpty()) {
		FILE* f = fopen(m_MasterLogFileName, "at+");
		if (AddNewLine)
			fprintf(f, "%s\n", lpszStatus);
		else
			fprintf(f, "%s", lpszStatus);
		fclose(f);
	}
}

void LTKCL_PrintStatus(LPCTSTR lpszStatus, BOOL AddNewLine)
{
	// prints status info to stdout
	if (!m_RunQuiet) {
		if (AddNewLine)
			printf("%s\n", lpszStatus);
		else
			printf("%s", lpszStatus);
	}
	LTKCL_PrintToLog(lpszStatus, AddNewLine);
}

void LTKCL_PrintVerboseStatus(LPCTSTR lpszStatus, BOOL AddNewLine, int Level)
{
	// prints status info to stdout
	if (!m_RunQuiet && m_ShowVerboseStatus && Level <= m_VerboseLevel) {
		CString csTemp;

		time_t aclock;
		time(&aclock);                 // Get time in seconds
	csTemp.Format("(elapsed time since start: %I64i seconds)", static_cast<long long>(aclock - m_StartTime));

		if (AddNewLine)
			printf("%s %s\n", lpszStatus, (LPCTSTR)csTemp);
		else
			printf("%s", lpszStatus);							// don't include elapsed time
//			printf("%s %s", lpszStatus, (LPCTSTR) csTemp);		// include elapsed time

		// 3/1/2022 added ability to send verbose messages to log file. use /verbose:100 to activate
		if (m_VerboseLevel == 100)
			LTKCL_PrintToLog(lpszStatus, AddNewLine);
	}
}

void LTKCL_ReportProductFile(LPCTSTR lpszFileName, LPCTSTR lpszProductType)
{
	// reports a product from processing...includes date and time for the file
	// file should be closed and should not be written to after the call to report the product
	// to maintain the time stamp
	char* months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char* ampm[] = {"AM", "PM"};

	if (!m_RunQuiet) {
		// get the file info
		WIN32_FILE_ATTRIBUTE_DATA attribdata;
		FILETIME filetime;
		SYSTEMTIME  systime;
		if (GetFileAttributesEx(lpszFileName, GetFileExInfoStandard, &attribdata)) {
			FileTimeToLocalFileTime(&attribdata.ftLastWriteTime, &filetime);
			FileTimeToSystemTime(&filetime, &systime);

			// check for am or pm and adjust the hours accordingly
			int pmflag = 0;
			if (systime.wHour > 12) {
				pmflag = 1;
				systime.wHour -= 12;
			}

			CString csTemp;
			csTemp.Format("%s file produced:\n   %s    %s %i, %i @ %i:%02i %s\n", lpszProductType, lpszFileName, months[systime.wMonth], systime.wDay, systime.wYear, systime.wHour, systime.wMinute, ampm[pmflag]);

			LTKCL_PrintStatus((LPCTSTR) csTemp, FALSE);
		}
	}
}

void LTKCL_PrintProgramHeader()
{
	CString csTemp;
#if defined(_M_X64)
	csTemp.Format("%s v%.2f 64-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#elif defined(_M_IX86)
	csTemp.Format("%s v%.2f 32-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#else
	csTemp.Format("%s v%.2f ??-bit (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);
#endif

//	csTemp.Format("%s v%.2f (FUSION v%.2lf) (Built on %s %s)", PROGRAM_NAME, PROGRAM_VERSION, FUSION_VERSION, __DATE__, __TIME__);

#ifdef _DEBUG
		csTemp += _T(" DEBUG");
#endif

#ifdef USE_LASLIB
		csTemp += _T(" (LASlib)");
#endif
	// prints the program header with the program name, version, build date
	LTKCL_PrintStatus((LPCTSTR) csTemp);

	LTKCL_PrintStatus(" --Robert J. McGaughey--USDA Forest Service--Pacific Northwest Research Station");

#ifdef LASZIP_API_H
	// only check for laszip.dll if needed...this prevents problems related to not having the laszip_api.h file included
	strcpy(m_LASzip_DLL_Name, "");
#if defined(_M_X64)
	strcpy(m_LASzip_DLL_Name, "LASzip64.dll");
	HMODULE hMod = GetModuleHandle(m_LASzip_DLL_Name);
#elif defined(_M_IX86)
	strcpy(m_LASzip_DLL_Name, "LASzip.dll");
	HMODULE hMod = GetModuleHandle(m_LASzip_DLL_Name);
#endif
	if (hMod != NULL) {
		csTemp.Format("   Using %s (c) Martin Isenburg--Rapidlasso for LAS/LAZ file access", m_LASzip_DLL_Name);
		LTKCL_PrintStatus((LPCTSTR) csTemp);

		// get version of LASzip DLL
		laszip_U8 version_major;
		laszip_U8 version_minor;
		laszip_U16 version_revision;
		laszip_U32 version_build;

		if (!laszip_get_version(&version_major, &version_minor, &version_revision, &version_build)) {
			csTemp.Format("   --%s V%d.%d r%d (build %d)", m_LASzip_DLL_Name, (int)version_major, (int)version_minor, (int)version_revision, (int)version_build);
			LTKCL_PrintStatus((LPCTSTR)csTemp);
		}
	}
#endif
}

void LTKCL_PrintProgramDescription()
{
	CString csTemp;
	csTemp.Format("%s", PROGRAM_DESCRIPTION);

	// prints the program header with the program name, version, build date
	LTKCL_PrintStatus((LPCTSTR) csTemp);
}

void LTKCL_PrintRunTime()
{
	CString csTemp;

	// prints the run header with the run date and time
	struct tm *newtime;
	newtime = localtime(&m_StartTime);  // Convert time to struct tm form 
	csTemp.Format("Run started: %s", asctime(newtime));
	csTemp.Replace("\n", "");
	LTKCL_PrintStatus((LPCTSTR) csTemp);
}

void LTKCL_PrintCommandLine()
{
	CString csTemp;
	csTemp.Format("Command line: %s", (LPCTSTR) m_clp.CommandLine());

	// prints the run header with the run date and time
	LTKCL_PrintStatus((LPCTSTR) csTemp);
}

void LTKCL_PrintElapsedTime(LPCTSTR Message)
{
	CString csTemp;

	time_t aclock;
	time(&aclock);                 // Get time in seconds
	csTemp.Format("%s elapsed time since start: %I64i seconds", Message, static_cast<long long>(aclock - m_StartTime));

	LTKCL_PrintStatus((LPCTSTR) csTemp);
}

void LTKCL_PrintEndReport(int ErrCode)
{
	CString csTemp;

	struct tm *newtime;
	time_t aclock;
	time(&aclock);                 // Get time in seconds
	newtime = localtime(&aclock);  // Convert time to struct tm form
	char timestring[26];
	strcpy(timestring, asctime(newtime));
	timestring[24] = '\0';
	csTemp.Format("Run finished: %s (elapsed time: %I64i seconds)", timestring, static_cast<long long>(aclock - m_StartTime));

	if (ErrCode)
		csTemp += _T("\n***There were errors during the run");

	csTemp += _T("\nDone");

	LTKCL_PrintStatus(csTemp);

	// print the csv log file entry
	struct tm *temptime;
	temptime = localtime(&m_StartTime);  // Convert time to struct tm form 
	char temptimestring[26];
	strcpy(temptimestring, asctime(temptime));
	temptimestring[24] = '\0';

	CString Status;
	if (ErrCode)
		Status = _T("Errors");
	else
		Status = _T("Success");

	csTemp.Format("\"%s\",%.2f,\"%s %s\",\"%s\",\"%s\",\"%s\",%I64i,%s", PROGRAM_NAME, PROGRAM_VERSION, __DATE__, __TIME__, (LPCTSTR)m_clp.ParamLine(), temptimestring, timestring, static_cast<long long>(aclock - m_StartTime), (LPCTSTR) Status);
	LTKCL_PrintToCSVLog((LPCTSTR) csTemp);
}

void LTKCL_LaunchHelpPage()
{
	// launch web page for help info...will silently fail if file doesn't exist
	CString HTMLFile;
	HTMLFile.Format("doc\\%s.htm", PROGRAM_NAME);
	::ShellExecute(NULL, "open", HTMLFile, NULL, NULL, SW_SHOWNORMAL);
}

BOOL LTKCL_VerifyPointFileFormatIsValid(LPCTSTR szFileName)
{
	// check file to see if it is a valid point file...does not check for compressed files (LAZ) and LASzip.dll
	char signature[9];
	int FileIsValid = FALSE;

	// if we are skipping the file check for valid point files, return TRUE...
	// It is likely that programs will fail if there are bad files but warnings should be given
	// when things fail.
	if (m_SkipFileCheck)
		return(TRUE);

	// determine file format by looking at first 8 bytes...ASCII or binary
	FILE* f = fopen(szFileName, "rb");

	if (f) {
		if (fread(signature, sizeof(char), 8, f) == 8) {
			signature[8] = '\0';

			if (strcmp(signature, "LIDARDAT") == 0) {
				// if ASCII use CDataFile member to access
				FileIsValid = TRUE;
			}
			else if (strcmp(signature, "LIDARBIN") == 0) {
				FileIsValid = TRUE;
			}
			else {
				// look at first 4 characters for LAS format signature...could be LAS or LAZ
				signature[4] = '\0';
				if (strcmp(signature, "LASF") == 0) {
					FileIsValid = TRUE;
				}
			}
		}
		fclose(f);
	}

	return(FileIsValid);
}
