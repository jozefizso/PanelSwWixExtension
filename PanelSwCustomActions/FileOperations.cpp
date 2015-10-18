#include "FileOperations.h"
#include "../CaCommon/WixString.h"
#include <Shellapi.h>

#define DeletePath_QUERY L"SELECT `Id`, `Path`, `Condition` FROM `PSW_DeletePath`"
enum DeletePathQuery { Id = 1, Path = 2, Condition = 3 };

extern "C" __declspec(dllexport) UINT DeletePath(MSIHANDLE hInstall)
{
	HRESULT hr = S_OK;
	UINT er = ERROR_SUCCESS;
	PMSIHANDLE hView;
	PMSIHANDLE hRecord;
	CFileOperations rollbackCAD;
	CFileOperations deferredFileCAD;
	CFileOperations commitCAD;
	CWixString tempPath;
	CComBSTR szCustomActionData;
	DWORD dwRes = 0;
	DWORD dwUnique = 0;

	hr = WcaInitialize(hInstall, __FUNCTION__);
	BreakExitOnFailure(hr, "Failed to initialize");
	WcaLog(LOGMSG_STANDARD, "Initialized.");

	// Ensure table PSW_DeletePath exists.
	hr = WcaTableExists(L"PSW_DeletePath");
	BreakExitOnFailure(hr, "Table does not exist 'PSW_DeletePath'. Have you authored 'PanelSw:DeletePath' entries in WiX code?");

	// Execute view
	hr = WcaOpenExecuteView(DeletePath_QUERY, &hView);
	BreakExitOnFailure1(hr, "Failed to execute SQL query '%ls'.", DeletePath_QUERY);
	WcaLog(LOGMSG_STANDARD, "Executed query.");

	// Get temporay folder
	dwRes = ::GetTempPath(dwRes, (LPWSTR)tempPath);
	BreakExitOnNullWithLastError(dwRes, hr, "Failed getting temporary folder");

	hr = tempPath.Allocate(dwRes + 1);
	BreakExitOnFailure(hr, "Failed allocating memory");

	dwRes = ::GetTempPath(dwRes + 1, (LPWSTR)tempPath);
	BreakExitOnNullWithLastError(dwRes, hr, "Failed getting temporary folder");
	BreakExitOnNull((dwRes < tempPath.Capacity()), hr, E_FAIL, "Failed getting temporary folder");

	// Iterate records
	while ((hr = WcaFetchRecord(hView, &hRecord)) != E_NOMOREITEMS)
	{
		BreakExitOnFailure(hr, "Failed to fetch record.");

		// Get fields
		CWixString szId, szFilePath, szRegex, szReplacement, szCondition;
		CWixString tempFile;
		int nIgnoreCase = 0;

		hr = WcaGetRecordString(hRecord, DeletePathQuery::Id, (LPWSTR*)szId);
		BreakExitOnFailure(hr, "Failed to get Id.");
		hr = WcaGetRecordFormattedString(hRecord, DeletePathQuery::Path, (LPWSTR*)szFilePath);
		BreakExitOnFailure(hr, "Failed to get Path.");
		hr = WcaGetRecordString(hRecord, DeletePathQuery::Condition, (LPWSTR*)szCondition);
		BreakExitOnFailure(hr, "Failed to get Condition.");

		// Test condition
		MSICONDITION condRes = ::MsiEvaluateConditionW(hInstall, szCondition);
		switch (condRes)
		{
		case MSICONDITION::MSICONDITION_NONE:
		case MSICONDITION::MSICONDITION_TRUE:
			WcaLog(LOGMSG_STANDARD, "Condition evaluated to true / none.");
			break;

		case MSICONDITION::MSICONDITION_FALSE:
			WcaLog(LOGMSG_STANDARD, "Skipping. Condition evaluated to false");
			continue;

		case MSICONDITION::MSICONDITION_ERROR:
			hr = E_FAIL;
			BreakExitOnFailure(hr, "Bad Condition field");
		}

		// Generate temp file name.
		hr = tempFile.Allocate(MAX_PATH + 1);
		BreakExitOnFailure(hr, "Failed allocating memory");

		dwRes = ::GetTempFileName((LPCWSTR)tempPath, L"DLT", ++dwUnique, (LPWSTR)tempFile);
		BreakExitOnNullWithLastError(dwRes, hr, "Failed getting temporary file name");

		hr = rollbackCAD.AddMoveFile((LPCWSTR)tempFile, szFilePath);
		BreakExitOnFailure(hr, "Failed creating custom action data for rollback action.");

		// Add deferred data to move file szFilePath -> tempFile.
		hr = deferredFileCAD.AddMoveFile(szFilePath, (LPCWSTR)tempFile);
		BreakExitOnFailure(hr, "Failed creating custom action data for deferred file action.");

		hr = commitCAD.AddDeleteFile((LPCWSTR)tempFile);
		BreakExitOnFailure(hr, "Failed creating custom action data for commit action.");
	}

	hr = rollbackCAD.GetCustomActionData(&szCustomActionData);
	BreakExitOnFailure(hr, "Failed getting custom action data for deferred action.");
	hr = WcaDoDeferredAction(L"DeletePath_rollback", szCustomActionData, rollbackCAD.GetCost());
	BreakExitOnFailure(hr, "Failed scheduling deferred action.");

	szCustomActionData.Empty();
	hr = deferredFileCAD.GetCustomActionData(&szCustomActionData);
	BreakExitOnFailure(hr, "Failed getting custom action data for deferred action.");
	hr = WcaDoDeferredAction(L"DeletePath_deferred", szCustomActionData, deferredFileCAD.GetCost());
	BreakExitOnFailure(hr, "Failed scheduling deferred action.");

	szCustomActionData.Empty();
	hr = commitCAD.GetCustomActionData(&szCustomActionData);
	BreakExitOnFailure(hr, "Failed getting custom action data for deferred action.");
	hr = WcaDoDeferredAction(L"DeletePath_commit", szCustomActionData, commitCAD.GetCost());
	BreakExitOnFailure(hr, "Failed scheduling deferred action.");

LExit:
	er = SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
	return WcaFinalize(er);
}

HRESULT CFileOperations::AddCopyFile(LPCWSTR szFrom, LPCWSTR szTo)
{
	HRESULT hr = S_OK;
	CComPtr<IXMLDOMElement> pElem;

	hr = AddElement(L"CopyFile", L"CFileOperations", 1, &pElem);
	BreakExitOnFailure(hr, "Failed to add XML element");

	hr = pElem->setAttribute(CComBSTR("From"), CComVariant(szFrom));
	BreakExitOnFailure(hr, "Failed to add XML attribute 'From'");

	hr = pElem->setAttribute(CComBSTR("To"), CComVariant(szTo));
	BreakExitOnFailure(hr, "Failed to add XML attribute 'To'");

LExit:
	return hr;
}

HRESULT CFileOperations::AddMoveFile(LPCWSTR szFrom, LPCWSTR szTo)
{
	HRESULT hr = S_OK;
	CComPtr<IXMLDOMElement> pElem;

	hr = AddElement(L"MoveFile", L"CFileOperations", 1, &pElem);
	BreakExitOnFailure(hr, "Failed to add XML element");

	hr = pElem->setAttribute(CComBSTR("From"), CComVariant(szFrom));
	BreakExitOnFailure(hr, "Failed to add XML attribute 'From'");

	hr = pElem->setAttribute(CComBSTR("To"), CComVariant(szTo));
	BreakExitOnFailure(hr, "Failed to add XML attribute 'To'");

LExit:
	return hr;
}

HRESULT CFileOperations::AddDeleteFile(LPCWSTR szPath)
{
	HRESULT hr = S_OK;
	CComPtr<IXMLDOMElement> pElem;

	hr = AddElement(L"DeleteFile", L"CFileOperations", 1, &pElem);
	BreakExitOnFailure(hr, "Failed to add XML element");

	hr = pElem->setAttribute(CComBSTR("Path"), CComVariant(szPath));
	BreakExitOnFailure(hr, "Failed to add XML attribute 'Path'");

LExit:
	return hr;
}

// Execute the command object (XML element)
HRESULT CFileOperations::DeferredExecute(IXMLDOMElement* pElem)
{
	HRESULT hr = S_OK;
	CComBSTR vTag;

	hr = pElem->get_tagName( &vTag);
	BreakExitOnFailure(hr, "Failed to get tag name");

	if( vTag == L"CopyFile")
	{
		hr = CopyFile(pElem);
		BreakExitOnFailure(hr, "Failed to copy file");
	}
	else if( vTag == L"MoveFile")
	{
		hr = MoveFile(pElem);
		BreakExitOnFailure(hr, "Failed to move file");
	}
	else if( vTag == L"DeleteFile")
	{
		hr = DeleteFile(pElem);
		BreakExitOnFailure(hr, "Failed to delete file");
	}
	else
	{
		hr = E_INVALIDARG;
		BreakExitOnFailure1(hr, "Invalid tag: '%ls'", (LPWSTR)vTag);
	}

LExit:
	return hr;
}

HRESULT CFileOperations::CopyFile(IXMLDOMElement* pElem)
{
	CComVariant vFrom;
	CComVariant vTo;
	CWixString sFrom;
	CWixString sTo;
	SHFILEOPSTRUCT opInfo;
	HRESULT hr = S_OK;
	INT nRes = ERROR_SUCCESS;

	hr = pElem->getAttribute(CComBSTR(L"From"), &vFrom);
	BreakExitOnFailure(hr, "Failed getting 'From' attribute");

	hr = pElem->getAttribute(CComBSTR(L"To"), &vTo);
	BreakExitOnFailure(hr, "Failed getting 'To' attribute");

	hr = sFrom.Format(L"%s\0\0", (LPCWSTR)vFrom.bstrVal);
	BreakExitOnFailure(hr, "Failed formatting string");

	hr = sTo.Format(L"%s\0\0", (LPCWSTR)vTo.bstrVal);
	BreakExitOnFailure(hr, "Failed formatting string");

	// Prepare 
	::memset(&opInfo, 0, sizeof(opInfo));
	opInfo.wFunc = FO_COPY;
	opInfo.pFrom = (LPCWSTR)sFrom;
	opInfo.pTo = (LPCWSTR)sTo;
	opInfo.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_NO_UI;

	nRes = ::SHFileOperation(&opInfo);
	BreakExitOnNull1((!nRes), hr, E_FAIL, "Failed copying file (Error %i)", nRes);
	BreakExitOnNull((!opInfo.fAnyOperationsAborted), hr, E_FAIL, "Failed copying file (operation aborted)");
	WcaLog(LOGLEVEL::LOGMSG_STANDARD, "Copied '%ls' to '%ls'", vFrom.bstrVal, vTo.bstrVal);

LExit:
	return hr;
}

HRESULT CFileOperations::MoveFile(IXMLDOMElement* pElem)
{
	CComVariant vFrom;
	CComVariant vTo;
	CWixString sFrom;
	CWixString sTo;
	SHFILEOPSTRUCT opInfo;
	HRESULT hr = S_OK;
	INT nRes = ERROR_SUCCESS;

	hr = pElem->getAttribute(CComBSTR(L"From"), &vFrom);
	BreakExitOnFailure(hr, "Failed getting 'From' attribute");

	hr = pElem->getAttribute(CComBSTR(L"To"), &vTo);
	BreakExitOnFailure(hr, "Failed getting 'To' attribute");

	hr = sFrom.Format(L"%s\0\0", (LPCWSTR)vFrom.bstrVal);
	BreakExitOnFailure(hr, "Failed formatting string");

	hr = sTo.Format(L"%s\0\0", (LPCWSTR)vTo.bstrVal);
	BreakExitOnFailure(hr, "Failed formatting string");

	// Prepare 
	::memset(&opInfo, 0, sizeof(opInfo));
	opInfo.wFunc = FO_MOVE;
	opInfo.pFrom = (LPCWSTR)sFrom;
	opInfo.pTo = (LPCWSTR)sTo;
	opInfo.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_NO_UI;

	nRes = ::SHFileOperation(&opInfo);
	BreakExitOnNull1((!nRes), hr, E_FAIL, "Failed moving file (Error %i)", nRes);
	BreakExitOnNull((!opInfo.fAnyOperationsAborted), hr, E_FAIL, "Failed moving file (operation aborted)");
	WcaLog(LOGLEVEL::LOGMSG_STANDARD, "Moved '%ls' to '%ls'", vFrom.bstrVal, vTo.bstrVal);

LExit:
	return hr;
}

HRESULT CFileOperations::DeleteFile(IXMLDOMElement* pElem)
{
	CComVariant vPath;
	CWixString sPath;
	SHFILEOPSTRUCT opInfo;
	HRESULT hr = S_OK;
	INT nRes = ERROR_SUCCESS;

	hr = pElem->getAttribute(CComBSTR(L"Path"), &vPath);
	BreakExitOnFailure(hr, "Failed getting 'Path' attribute");

	hr = sPath.Format(L"%s\0\0", (LPCWSTR)vPath.bstrVal);
	BreakExitOnFailure(hr, "Failed formatting string");

	// Prepare 
	::memset(&opInfo, 0, sizeof(opInfo));
	opInfo.wFunc = FO_DELETE;
	opInfo.pFrom = (LPCWSTR)sPath;
	opInfo.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_NO_UI;

	nRes = ::SHFileOperation(&opInfo);
	BreakExitOnNull1((!nRes), hr, E_FAIL, "Failed deleting file (Error %i)", nRes);
	BreakExitOnNull((!opInfo.fAnyOperationsAborted), hr, E_FAIL, "Failed deleting file (operation aborted)");
	WcaLog(LOGLEVEL::LOGMSG_STANDARD, "Deleted '%ls'", vPath.bstrVal);

LExit:
	return hr;
}
