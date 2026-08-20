// LIDAR Toolkit
// Bob McGaughey, USDA Forest Service
//

// macros
#define I32_QUANTIZE(n) (((n) >= 0) ? (int)((n)+0.5f) : (int)((n)-0.5f))

typedef unsigned __int64 uint64;

// global parameters common to all command line utilities
int m_nRetCode;
CCommandLineParameters m_clp;
time_t m_StartTime;
CString m_MasterLogFileName;
CString m_MasterCSVLogFileName;

// command line switches common to all LTKCL utilities
BOOL m_RunQuiet;			// /quiet
BOOL m_ReportVersionInfo;	// /version
BOOL m_RunInteractive;		// /interactive
BOOL m_ClearMasterLogFile;	// /newlog
BOOL m_ShowVerboseStatus;	// /verbose
BOOL m_UseLocale;			// /locale
BOOL m_SkipFileCheck;		// skip checking for valid point files
BOOL m_NoLASzip_DLL;		// /nolaszipdll
char m_LASzip_DLL_Name[16];	// name of LASzip.dll...changes depending on 32- or 64-bit build

int m_VerboseLevel;			// /verbose:#...low #: less info, 99: all info

BOOL m_NoLogOutput = FALSE;	// diasbles log file output...set from programs not a switch

// function prototypes for "standard" functions
// these functions are NOT TYPICALLY changed when creating a new utility program
void LTKCL_PrintStatus(LPCTSTR lpszStatus, BOOL AddNewLine = TRUE);
void LTKCL_PrintVerboseStatus(LPCTSTR lpszStatus, BOOL AddNewLine = TRUE, int Level = 1);
void LTKCL_PrintStandardSwitchInfo(BOOL UsesPoints = TRUE);
void LTKCL_PrintToLog(LPCTSTR lpszStatus, BOOL AddNewLine = TRUE);
void LTKCL_PrintProgramHeader();
void LTKCL_PrintProgramDescription();
void LTKCL_PrintRunTime();
void LTKCL_PrintCommandLine();
void LTKCL_PrintEndReport(int ErrCode);
void LTKCL_PrintElapsedTime(LPCTSTR Message);
void LTKCL_ReportProductFile(LPCTSTR lpszFileName, LPCTSTR lpszProductType);
void LTKCL_LaunchHelpPage();
BOOL LTKCL_Initialize();
void LTKCL_ParseStandardSwitches();
BOOL LTKCL_VerifyCommandLine(const char* ValidSwitches = "");
BOOL LTKCL_VerifyPointFileFormatIsValid(LPCTSTR szFileName);

