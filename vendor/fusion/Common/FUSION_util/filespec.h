// filespec.h class definition for file specifier class

#ifndef _FILESPEC
#define _FILESPEC


class CFileSpec
{
public:
	LPCTSTR operator=(LPCTSTR spec);

	void SetDirectory( LPCTSTR directory );
	LPCTSTR GetShortPathName();
	void SetTitle( LPCTSTR title ) {
		   CString FileName = title;
		   FileName  += m_ext;
		   SetFileNameEx( FileName);
	}

	enum FS_BUILTINS {
			FS_EMPTY,			//   Nothing
			FS_APP,				//   Full application path and name
			FS_APPDIR,			//   Application folder
			FS_WINDIR,			//   Windows folder
			FS_SYSDIR,			//   System folder
			FS_TMPDIR,			//   Temporary folder
			FS_DESKTOP,			//       Desktop folder
			FS_FAVOURITES,		//       Favourites folder
			FS_TEMPNAME			//       Create a temporary name
	};

	CFileSpec(FS_BUILTINS spec = FS_EMPTY);

	CFileSpec(LPCTSTR spec);

	//      Operations
	BOOL                    Exists();

	//      Access functions
	CString&    Drive() { return m_drive; }
	CString&    Path()  { return m_path; }
	CString     Directory() { return m_drive + m_path; }
	CString     FileName()  { return m_fileName + m_ext; }
	CString&    FileTitle() { return m_fileName; }
	CString&    Extension() { return m_ext; }
	CString     FullPathNoExtension();

	operator LPCTSTR() { 
		static CString temp;
		temp = GetFullSpec(); 
		return temp;
	}          // as a C string

	CString GetFullSpec();
	void    SetFullSpec(LPCTSTR spec);
	void    SetFullSpec(FS_BUILTINS spec = FS_EMPTY);
       
	CString GetFileNameEx();
	void    SetFileNameEx(LPCTSTR spec);
	void    SetExt( LPCTSTR Ext ) {
	   m_ext = Ext;
	}
	void    SetDrive( LPCTSTR Drive ) {		// Drive letter and colon
	   m_drive = Drive;
	}

private:
	void    Initialize();
	void    Initialize(FS_BUILTINS spec);
	void    UnLock();
	void    GetShellFolder(LPCTSTR folder);

	CString m_drive, m_path, m_fileName, m_ext;
};

#endif

