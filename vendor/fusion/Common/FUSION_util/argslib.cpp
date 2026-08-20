//***********************************************************************
// (c) Copyright 1999-2003 Santronics Software, Inc. All Rights Reserved.
//***********************************************************************
// File Name : argslib.cpp
// Subsystem : Command Line Parameters Wrapper
// Date      : 03/03/2003
// Author    : Hector Santos, Santronics Software, Inc.
// VERSION   : 1.00P
//
// Revision History:
// Version  Date      Author  Comments
// -------  --------  ------  -------------------------------------------
// v1.00P   03/03/03  HLS     Public Release version (non-SSL version)
//
// 8/25/2009
// RJM:  added logic to remove quotation marks from switch parameters and 
// command line arguments
//
// 6/25/2013
// RJM: Fixed a problem when the parsing logic was not being used with the actual command line passed to a progam. In the default
// constructor, the original command line for the progam was duplicated. When you called SetCommandLineParameters() with new command string,
// the original copy was not deleted so you had a memory leak each time you called SetCommandLineParameters().
//
//***********************************************************************

#include "stdafx.h"
#include "argslib.h"


///////////////////////////////////////////////////////////////
// cut/paste example usage:
/*
    #include "argslib.h"


    CCommandLineParameters clp;
    if (clp.CheckHelp(FALSE) {  // set TRUE to display help if no switches
        ... display help ...
        return;
    }
    CString sServer = clp.GetSwitchStr("server");
    if (clp.Switch("debug")) DebugLogging     = TRUE;
    if (clp.Switch("dlog+")) DailyLogsEnabled = TRUE;
    if (clp.Switch("dlog-")) DailyLogsEnabled = FALSE;
    if (clp.Switch("trace")) EnableTraceLog   = TRUE;

    if (sServer.IsEmpty()) {
        if (!WildcatServerConnect(NULL)) {
            return FALSE;
        }
    } else {
        if (!WildcatServerConnectSpecific(NULL,sServer)) {
            return FALSE;
        }
    }
*/
///////////////////////////////////////////////////////////////

//-------------------------------------------------------------
// CreateParameterFromString()
//
// Given command line string, generate array of strings

int CreateParameterFromString(char *pszParams, char *argv[], int max)
{
    int argc = 0;
    if (pszParams) {
        char *p = pszParams;
        while (*p && (argc < max)) {
            while (*p == ' ') {
                p++;
            }
            if (!*p) {
                break;
            }
            if (*p == '"') {
                p++;
                argv[argc++] = p;
                while (*p && *p != '"') {
                    p++;
                }
            } else {
                argv[argc++] = p;
                while (*p && *p != ' ') {
                    p++;
                }
            }
            if (*p) {
                *p++ = 0;
            }
        }
    }
    return argc;
}

CCommandLineParameters::CCommandLineParameters(char *cl)
{
	maxparms = 100;
	for (int i = 0; i < 100; i++)
		parms[i] = NULL;

	SetCommandLineParameters(cl);
}

CCommandLineParameters::CCommandLineParameters(const char *switchchars /* = "-/" */)
    : szSwitchChars(switchchars)
{
    maxparms = 100;
	for (int i = 0; i < 100; i++)
		parms[i] = NULL;

	pszCmdLineDup = _strdup(GetCommandLine());
    paramcount = CreateParameterFromString(pszCmdLineDup,parms,maxparms);
}

CCommandLineParameters::~CCommandLineParameters()
{
    if (pszCmdLineDup) {
      free(pszCmdLineDup);
      pszCmdLineDup = NULL;
    }
}

void CCommandLineParameters::SetCommandLineParameters(const char *cl)
{
    maxparms = 100;
    if (pszCmdLineDup)
      free(pszCmdLineDup);

    pszCmdLineDup = _strdup(cl);
    paramcount = CreateParameterFromString(pszCmdLineDup,parms,maxparms);
}

BOOL CCommandLineParameters::CheckHelp(const BOOL bNoSwitches /*= FALSE */)
{
     if (bNoSwitches && (paramcount < 2)) return TRUE;
     if (paramcount < 2) return FALSE;
     if (strcmp(ParamStr(1),"-?") == 0) return TRUE;
     if (strcmp(ParamStr(1),"/?") == 0) return TRUE;
     if (strcmp(ParamStr(1),"?") == 0) return TRUE;
     return FALSE;
}

CString CCommandLineParameters::ParamStr(const int index, const BOOL bGetAll /* = FALSE */)
{
    if ((index < 0) || (index >= paramcount)) {
        return "";
    }
    CString s = parms[index];
    if (bGetAll) {
        for (int i = index+1;i < paramcount; i++) {
              s += " ";
              s += parms[i];
        }
    }
    return s;
}

int CCommandLineParameters::ParamInt(const int index)
{
    return atoi(ParamStr(index));
}

CString CCommandLineParameters::ParamLine()
{
    CString s;
    char *p = strchr(GetCommandLine(),' ');
    if (p) {
        s.Format("%s",p+1);
    }
    return s;
}

CString CCommandLineParameters::CommandLine()
{
    CString s;
    s.Format("%s",GetCommandLine());
    return s;
}

BOOL CCommandLineParameters::IsSwitch(const char *sz)
{
    return (strchr(szSwitchChars,sz[0]) != NULL);
}


int CCommandLineParameters::SwitchCount()
{
    int count = 0;
    for (int i = 1;i < paramcount; i++) {
        if (IsSwitch(parms[i])) count++;
    }
    return count;
}

int CCommandLineParameters::FirstNonSwitchIndex()
{
    for (int i = 1;i < paramcount; i++) {
        if (!IsSwitch(parms[i])) {
            return i;
        }
    }
    return 0;
}

CString CCommandLineParameters::FirstNonSwitchStr()     // 499.5 04/16/01 12:17 am
{
    // returns the first non-switch, handles lines such as:
    // [options] file [specs]
    return GetNonSwitchStr(FALSE,TRUE);
}

//////////////////////////////////////////////////////////////////////////
// SwitchIndex() will return TRUE if the switch exists and FALSE if not.
//
// Changed things a bit to fix a problem in the way I was using the Switch()
// function. Changed the original Switch() function to SwitchIndex() and 
// added a new Switch() function that returned a BOOL type. The original 
// Switch() function return an int that was the index of the switch in the 
// command line. I had been using this with BOOL type variables and this
// was working because BOOL maps to int type.
//
BOOL CCommandLineParameters::Switch(const char *sz,
									const BOOL bCase /* = FALSE */
)
{
	return(SwitchIndex(sz, bCase) != 0);
}

//////////////////////////////////////////////////////////////////////////
// SwitchIndex() will return the parameter index if the switch exist.
// Return 0 if not found.  The logic will allow for two types of
// switches:
//
//          /switch value
//          /switch:value
//
// DO NOT PASS THE COLON. IT IS AUTOMATICALLY CHECKED.  In other
// words, the following statements are the same:
//
//         Switch("server");
//         Switch("-server");
//         Switch("/server");
//
// to handle the possible arguments:
//
//         /server:value
//         /server value
//         -server:value
//         -server value
//

int CCommandLineParameters::SwitchIndex(const char *sz,
                                   const BOOL bCase /* = FALSE */
                                   )
{
    if (!sz || !sz[0]) {
        return 0;
    }

    char sz2[255];
    strncpy(sz2,sz,sizeof(sz2)-1);
    sz2[sizeof(sz2)-1] = 0;

    char *p = sz2;
    if (strchr(szSwitchChars,*p) != NULL) p++;

    // check for abbrevation
    int ful_amt = 0;
    int abr_amt = 0;
    char *abbr = strchr(p,'*');
    if (abbr) {
      *abbr = 0;
      abr_amt = (int) strlen(p);
      strcpy(abbr,abbr+1);
    }
    ful_amt = (int) strlen(p);
 
    for (int i = 1;i < paramcount; i++) {
      if (!IsSwitch(parms[i])) continue;
      char *pColon = strchr(&parms[i][1],':');
      int arg_amt = 0;
      if (pColon) {
        arg_amt = (int) (pColon - &parms[i][1]); 
      } else {
        arg_amt = (int) strlen(&parms[i][1]);
      }
      if (bCase) {
        if (arg_amt == ful_amt && strncmp(p,&parms[i][1],ful_amt) == 0) return i;
        if (arg_amt == abr_amt && strncmp(p,&parms[i][1],abr_amt) == 0) return i;
      } else {
        if (arg_amt == ful_amt && _strnicmp(p,&parms[i][1],ful_amt) == 0) return i;
        if (arg_amt == abr_amt && _strnicmp(p,&parms[i][1],abr_amt) == 0) return i;
      }
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////////
// GetSwitchStr() will return the string for the given switch. The logic
// will allow for two types of switches:
//
//   /switch value
//   /switch:value
//
// RJM:  changed logic to only allow /switch:value type
// RJM:  added Replace() to remove quotation marks from switch parameters

CString CCommandLineParameters::GetSwitchStr(const char *sz,
                                             const char *szDefault, /* = "" */
                                             const BOOL bCase /* = FALSE */
                                            )
{
    int idx = SwitchIndex(sz,bCase);
    if (idx > 0) {
        CString s = ParamStr(idx);
        int n = s.Find(':');
        if (n > -1) {
			CString ts = s.Mid(n + 1);
			ts.Replace("\"", "");
            return ts;
//            return s.Mid(n+1);
        } 
//		else {
//          if ((idx+1) < paramcount) {
//              if (!IsSwitch(parms[idx+1])) {
//                  return parms[idx+1];
//              }
//          }
//        }
        //return szDefault;
    }
    return szDefault;
}

int CCommandLineParameters::GetSwitchInt(const char *sz,
                                          const int iDefault, /* = 0 */
                                          const BOOL bCase /* = FALSE */
                                         )
{
    char szDefault[25];
    sprintf(szDefault,"%d",iDefault);
    return atoi(GetSwitchStr(sz,szDefault,bCase));
}

// RJM:  added Replace() to remove quotation marks from command line arguments...this should be handled by DOS
//		 but it won't hurt to make sure
CString CCommandLineParameters::GetNonSwitchStr(
                                const BOOL bBreakAtSwitch, /* = TRUE */
                                const BOOL bFirstOnly /* = FALSE */)
{
    CString sLine = "";
    int i = 1;
    while (i < paramcount) {
        if (IsSwitch(parms[i])) {
            if (bBreakAtSwitch) break;
        } else {
            sLine += parms[i];
            if (bFirstOnly) break;
            sLine += " ";
        }
        i++;
    }
    sLine.TrimRight();
	sLine.Replace("\"", "");
    return sLine;
}
