//***********************************************************************
// (c) Copyright 1999-2003 Santronics Software, Inc. All Rights Reserved.
//***********************************************************************
// File Name : argslib.h
// Subsystem : Command Line Parameters Wrapper
// Date      : 03/03/2003
// Author    : Hector Santos, Santronics Software, Inc.
// VERSION   : 1.00P
//
// Revision History:
// Version  Date      Author  Comments
// -------  --------  ------  -------------------------------------------
// v1.00P   03/03/03  HLS     Public Release version (non-SSL version)
//***********************************************************************

#ifndef __ARGSLIB_H
#define __ARGSLIB_H

#include <afx.h>
#include <string.h>


class CCommandLineParameters {
public:
    CCommandLineParameters(const char *switchchars = "/");
    CCommandLineParameters(char *cl);
	~CCommandLineParameters();
public:
	void SetCommandLineParameters(const char *cl);
    BOOL CheckHelp(const BOOL bNoSwitches = FALSE);

    int ParamCount() { return paramcount; }
    CString ParamLine();
    CString CommandLine();
    CString ParamStr(int index, const BOOL bGetAll = FALSE);
    int ParamInt(int index);

    int FirstNonSwitchIndex();
    CString FirstNonSwitchStr();
    int SwitchCount();
	BOOL Switch(const char *sz, const BOOL bCase = FALSE);
	int SwitchIndex(const char *sz, const BOOL bCase = FALSE);
	CString GetSwitchStr(const char *sz, const char *szDefault = "", const BOOL bCase = FALSE);
    int GetSwitchInt(const char *sz, const int iDefault = -1, const BOOL bCase = FALSE);
    CString GetNonSwitchStr(const BOOL bBreakAtSwitch = TRUE, const BOOL bFirstOnly = FALSE);


private:
    BOOL IsSwitch(const char *sz);
    const char *szSwitchChars;
    char *parms[100];
    char *pszCmdLineDup;
    int maxparms;
    int paramcount;
};


#endif // __ARGSLIB_H
