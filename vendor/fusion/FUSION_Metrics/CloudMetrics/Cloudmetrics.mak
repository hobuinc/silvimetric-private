# Microsoft Developer Studio Generated NMAKE File, Based on Cloudmetrics.dsp
!IF "$(CFG)" == ""
CFG=Cloudmetrics - Win32 Debug
!MESSAGE No configuration specified. Defaulting to Cloudmetrics - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "Cloudmetrics - Win32 Release" && "$(CFG)" != "Cloudmetrics - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
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
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Cloudmetrics - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release
# Begin Custom Macros
OutDir=.\Release
# End Custom Macros

ALL : "$(OUTDIR)\Cloudmetrics.exe"


CLEAN :
	-@erase "$(INTDIR)\argslib.obj"
	-@erase "$(INTDIR)\Cloudmetrics.obj"
	-@erase "$(INTDIR)\Cloudmetrics.pch"
	-@erase "$(INTDIR)\Cloudmetrics.res"
	-@erase "$(INTDIR)\DataCatalogEntry.obj"
	-@erase "$(INTDIR)\DataFile.obj"
	-@erase "$(INTDIR)\DataIndex.obj"
	-@erase "$(INTDIR)\filespec.obj"
	-@erase "$(INTDIR)\LASFormatFile.obj"
	-@erase "$(INTDIR)\LidarData.obj"
	-@erase "$(INTDIR)\plansdtm.obj"
	-@erase "$(INTDIR)\StdAfx.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\Cloudmetrics.exe"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MD /W3 /GX /O2 /I "..\common\FUSION_util" /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Fp"$(INTDIR)\Cloudmetrics.pch" /Yu"stdafx.h" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\Cloudmetrics.res" /d "NDEBUG" /d "_AFXDLL" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\Cloudmetrics.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /incremental:no /pdb:"$(OUTDIR)\Cloudmetrics.pdb" /machine:I386 /out:"$(OUTDIR)\Cloudmetrics.exe" 
LINK32_OBJS= \
	"$(INTDIR)\argslib.obj" \
	"$(INTDIR)\Cloudmetrics.obj" \
	"$(INTDIR)\DataCatalogEntry.obj" \
	"$(INTDIR)\DataFile.obj" \
	"$(INTDIR)\DataIndex.obj" \
	"$(INTDIR)\filespec.obj" \
	"$(INTDIR)\LASFormatFile.obj" \
	"$(INTDIR)\LidarData.obj" \
	"$(INTDIR)\plansdtm.obj" \
	"$(INTDIR)\StdAfx.obj" \
	"$(INTDIR)\Cloudmetrics.res"

"$(OUTDIR)\Cloudmetrics.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "Cloudmetrics - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug
# Begin Custom Macros
OutDir=.\Debug
# End Custom Macros

ALL : "$(OUTDIR)\Cloudmetrics.exe"


CLEAN :
	-@erase "$(INTDIR)\argslib.obj"
	-@erase "$(INTDIR)\Cloudmetrics.obj"
	-@erase "$(INTDIR)\Cloudmetrics.pch"
	-@erase "$(INTDIR)\Cloudmetrics.res"
	-@erase "$(INTDIR)\DataCatalogEntry.obj"
	-@erase "$(INTDIR)\DataFile.obj"
	-@erase "$(INTDIR)\DataIndex.obj"
	-@erase "$(INTDIR)\filespec.obj"
	-@erase "$(INTDIR)\LASFormatFile.obj"
	-@erase "$(INTDIR)\LidarData.obj"
	-@erase "$(INTDIR)\plansdtm.obj"
	-@erase "$(INTDIR)\StdAfx.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(INTDIR)\vc60.pdb"
	-@erase "$(OUTDIR)\Cloudmetrics.exe"
	-@erase "$(OUTDIR)\Cloudmetrics.ilk"
	-@erase "$(OUTDIR)\Cloudmetrics.pdb"

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

CPP_PROJ=/nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Fp"$(INTDIR)\Cloudmetrics.pch" /Yu"stdafx.h" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /GZ /c 
RSC_PROJ=/l 0x409 /fo"$(INTDIR)\Cloudmetrics.res" /d "_DEBUG" /d "_AFXDLL" 
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\Cloudmetrics.bsc" 
BSC32_SBRS= \
	
LINK32=link.exe
LINK32_FLAGS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /incremental:yes /pdb:"$(OUTDIR)\Cloudmetrics.pdb" /debug /machine:I386 /out:"$(OUTDIR)\Cloudmetrics.exe" /pdbtype:sept 
LINK32_OBJS= \
	"$(INTDIR)\argslib.obj" \
	"$(INTDIR)\Cloudmetrics.obj" \
	"$(INTDIR)\DataCatalogEntry.obj" \
	"$(INTDIR)\DataFile.obj" \
	"$(INTDIR)\DataIndex.obj" \
	"$(INTDIR)\filespec.obj" \
	"$(INTDIR)\LASFormatFile.obj" \
	"$(INTDIR)\LidarData.obj" \
	"$(INTDIR)\plansdtm.obj" \
	"$(INTDIR)\StdAfx.obj" \
	"$(INTDIR)\Cloudmetrics.res"

"$(OUTDIR)\Cloudmetrics.exe" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<


!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("Cloudmetrics.dep")
!INCLUDE "Cloudmetrics.dep"
!ELSE 
!MESSAGE Warning: cannot find "Cloudmetrics.dep"
!ENDIF 
!ENDIF 


!IF "$(CFG)" == "Cloudmetrics - Win32 Release" || "$(CFG)" == "Cloudmetrics - Win32 Debug"
SOURCE=..\..\common\FUSION_util\argslib.cpp

"$(INTDIR)\argslib.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\Cloudmetrics.cpp

"$(INTDIR)\Cloudmetrics.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"


SOURCE=.\Cloudmetrics.rc

"$(INTDIR)\Cloudmetrics.res" : $(SOURCE) "$(INTDIR)"
	$(RSC) $(RSC_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\DataCatalogEntry.cpp

"$(INTDIR)\DataCatalogEntry.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\DataFile.cpp

"$(INTDIR)\DataFile.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\DataIndex.cpp

"$(INTDIR)\DataIndex.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\filespec.cpp

"$(INTDIR)\filespec.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\LASFormatFile.cpp

"$(INTDIR)\LASFormatFile.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\LidarData.cpp

"$(INTDIR)\LidarData.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=..\..\common\FUSION_util\plansdtm.cpp

"$(INTDIR)\plansdtm.obj" : $(SOURCE) "$(INTDIR)" "$(INTDIR)\Cloudmetrics.pch"
	$(CPP) $(CPP_PROJ) $(SOURCE)


SOURCE=.\StdAfx.cpp

!IF  "$(CFG)" == "Cloudmetrics - Win32 Release"

CPP_SWITCHES=/nologo /MD /W3 /GX /O2 /I "..\common\FUSION_util" /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Fp"$(INTDIR)\Cloudmetrics.pch" /Yc"stdafx.h" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 

"$(INTDIR)\StdAfx.obj"	"$(INTDIR)\Cloudmetrics.pch" : $(SOURCE) "$(INTDIR)"
	$(CPP) @<<
  $(CPP_SWITCHES) $(SOURCE)
<<


!ELSEIF  "$(CFG)" == "Cloudmetrics - Win32 Debug"

CPP_SWITCHES=/nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\common\FUSION_util" /I "..\..\common\LTK" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /D "_AFXDLL" /Fp"$(INTDIR)\Cloudmetrics.pch" /Yc"stdafx.h" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /GZ /c 

"$(INTDIR)\StdAfx.obj"	"$(INTDIR)\Cloudmetrics.pch" : $(SOURCE) "$(INTDIR)"
	$(CPP) @<<
  $(CPP_SWITCHES) $(SOURCE)
<<


!ENDIF 


!ENDIF 

