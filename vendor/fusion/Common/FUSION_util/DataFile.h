// DataFile.h: interface for the CDataFile class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DATAFILE_H__BF8B5780_DC45_11D2_A124_FC0D08C10B02__INCLUDED_)
#define AFX_DATAFILE_H__BF8B5780_DC45_11D2_A124_FC0D08C10B02__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#define		READ		"rb"

#define		SKIPCOMMENTS			";%!"
#define		SKIPCOMMENTSCOMMANDS	";%!#"

class CDataFile  
{
public:
	BOOL ReadValueNAN(double *val);
	BOOL ReadValueWS(double *val);
	BOOL ReadValue(double* val);
	BOOL ReadRawBytes(char* buf, int cnt, BOOL LessIsOK = FALSE);
	int m_CurrentLineNumber;
	CString m_FileName;
	BOOL ReadAll(char* buf);
	long GetFileSize();
	CDataFile(LPCTSTR FileName);
	CDataFile();
	virtual ~CDataFile();

	BOOL NewReadASCIILine(char *buf, BOOL SkipBlanks = TRUE);
	BOOL ReadASCIILine(char *buf, BOOL SkipBlanks = TRUE);
	BOOL ReadDataLine(char *buf, LPCTSTR flagchr, BOOL SkipBlanks = TRUE);

	BOOL IsValid();
	BOOL Open(LPCTSTR FileName);
	void Close();

	BOOL SetPosition(long Offset, int FromWhere = SEEK_SET);
	long GetPosition();
	void Rewind();

private:
	void SetTerminatorLength();
	int m_TerminatorLength;
	FILE* m_FileHandle;
};

#endif // !defined(AFX_DATAFILE_H__BF8B5780_DC45_11D2_A124_FC0D08C10B02__INCLUDED_)
