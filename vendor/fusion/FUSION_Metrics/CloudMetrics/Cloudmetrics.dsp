# Microsoft Developer Studio Project File - Name="Cloudmetrics" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=Cloudmetrics - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "Cloudmetrics.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "Cloudmetrics.mak" CFG="Cloudmetrics - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Cloudmetrics - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "Cloudmetrics - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Cloudmetrics - Win32 Release"

# PROP BASE Use_MFC 2
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 2
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MD /W3 /GX /Od /I "..\common\FUSION_util" /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /I "\Bob's Stuff\Code from other people\lastools_latest" /D "NDEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Yu"stdafx.h" /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "NDEBUG" /d "_AFXDLL"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 /nologo /subsystem:console /machine:I386 /nodefaultlib:"LIBC" /out:"\fusion_demo\cloudmetrics.exe"

!ELSEIF  "$(CFG)" == "Cloudmetrics - Win32 Debug"

# PROP BASE Use_MFC 2
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 1
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /I "\Bob's Stuff\Code from other people\lastools_latest" /D "_DEBUG" /D "WIN32" /D "_CONSOLE" /D "_MBCS" /Yu"stdafx.h" /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG" /d "_AFXDLL"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 /nologo /subsystem:console /debug /machine:I386 /nodefaultlib:"LIBCMT" /out:"\fusion_demo\cloudmetrics.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "Cloudmetrics - Win32 Release"
# Name "Cloudmetrics - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\common\FUSION_util\argslib.cpp
# End Source File
# Begin Source File

SOURCE=.\Cloudmetrics.cpp
# End Source File
# Begin Source File

SOURCE=.\Cloudmetrics.rc
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataCatalogEntry.cpp
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataFile.cpp
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataIndex.cpp
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\filespec.cpp
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\LASFormatFile.cpp
# End Source File
# Begin Source File

SOURCE="..\..\..\Code from other people\lastools_latest\laszip_api.c"
# SUBTRACT CPP /YX /Yc /Yu
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\LidarData_LASlib.cpp
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\plansdtm.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\common\FUSION_util\argslib.h
# End Source File
# Begin Source File

SOURCE=.\Cloudmetrics.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataCatalogEntry.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataFile.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\DataIndex.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\filespec.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\LASFormatFile.h
# End Source File
# Begin Source File

SOURCE="..\..\..\Code from other people\lastools_latest\laszip_api.h"
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\LidarData_LASlib.h
# End Source File
# Begin Source File

SOURCE=..\..\common\FUSION_util\plansdtm.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=.\ReadMe.txt
# End Source File
# End Target
# End Project
