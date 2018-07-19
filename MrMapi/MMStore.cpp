#include <StdAfx.h>
#include <MrMapi/MrMAPI.h>
#include <MrMapi/MMStore.h>
#include <MrMapi/MMFolder.h>
#include <MAPI/ColumnTags.h>
#include <Interpret/InterpretProp.h>
#include <MAPI/MAPIFunctions.h>
#include <MAPI/MAPIStoreFunctions.h>
#include <Interpret/String.h>

HRESULT OpenStore(_In_ LPMAPISESSION lpMAPISession, ULONG ulIndex, _Out_ LPMDB* lppMDB)
{
	auto hRes = S_OK;
	LPMAPITABLE lpStoreTable = nullptr;
	LPMDB lpMDB = nullptr;
	if (!lpMAPISession || !lppMDB) return MAPI_E_INVALID_PARAMETER;

	*lppMDB = nullptr;

	hRes = WC_MAPI(lpMAPISession->GetMsgStoresTable(0, &lpStoreTable));

	if (lpStoreTable)
	{
		static const SizedSPropTagArray(1, sptStore) = {
			1,
			PR_ENTRYID,
		};
		WC_MAPI(lpStoreTable->SetColumns(LPSPropTagArray(&sptStore), TBL_ASYNC));

		if (SUCCEEDED(hRes))
		{
			LPSRowSet lpRow = nullptr;
			WC_MAPI(lpStoreTable->SeekRow(BOOKMARK_BEGINNING, ulIndex, NULL));
			WC_MAPI(lpStoreTable->QueryRows(1, NULL, &lpRow));
			if (SUCCEEDED(hRes) && lpRow && 1 == lpRow->cRows && PR_ENTRYID == lpRow->aRow[0].lpProps[0].ulPropTag)
			{
				hRes = WC_H(mapi::store::CallOpenMsgStore(
					lpMAPISession, NULL, &lpRow->aRow[0].lpProps[0].Value.bin, MDB_NO_DIALOG | MDB_WRITE, &lpMDB));
				if (SUCCEEDED(hRes) && lpMDB)
				{
					*lppMDB = lpMDB;
				}
				else if (SUCCEEDED(hRes))
				{
					hRes = MAPI_E_CALL_FAILED;
				}
			}

			if (lpRow) FreeProws(lpRow);
		}
	}

	if (lpStoreTable) lpStoreTable->Release();

	return hRes;
}

HRESULT HrMAPIOpenStoreAndFolder(
	_In_ LPMAPISESSION lpMAPISession,
	_In_ ULONG ulFolder,
	_In_ std::wstring lpszFolderPath,
	_Out_opt_ LPMDB* lppMDB,
	_Deref_out_opt_ LPMAPIFOLDER* lppFolder)
{
	auto hRes = S_OK;
	LPMDB lpMDB = nullptr;
	LPMAPIFOLDER lpFolder = nullptr;

	auto paths = strings::split(lpszFolderPath, L'\\');

	if (lpMAPISession)
	{
		// Check if we were told which store to open
		if (!paths.empty() && paths[0][0] == L'#')
		{
			// Skip the '#'
			auto root = paths[0].substr(1);
			paths.erase(paths.begin());
			lpszFolderPath = strings::join(paths, L'\\');

			auto bin = strings::HexStringToBin(root);
			// In order for cb to get bigger than 1, the string has to have at least 4 characters
			// Which is larger than any reasonable store number. So we use that to distinguish.
			if (bin.size() > 1)
			{
				SBinary Bin = {0};
				Bin.cb = static_cast<ULONG>(bin.size());
				Bin.lpb = bin.data();
				hRes = WC_H(mapi::store::CallOpenMsgStore(lpMAPISession, NULL, &Bin, MDB_NO_DIALOG | MDB_WRITE, &lpMDB));
			}
			else
			{
				hRes = S_OK;
				LPWSTR szEndPtr = nullptr;
				const auto ulStore = wcstoul(root.c_str(), &szEndPtr, 10);

				// Only '\' and NULL are acceptable next characters after our store number
				if (szEndPtr && (szEndPtr[0] == L'\\' || szEndPtr[0] == L'\0'))
				{
					// We have a store. Let's open it
					hRes = WC_H(OpenStore(lpMAPISession, ulStore, &lpMDB));
					lpszFolderPath = szEndPtr;
				}
				else
				{
					hRes = MAPI_E_INVALID_PARAMETER;
				}
			}
		}
		else
		{
			hRes = WC_H(OpenExchangeOrDefaultMessageStore(lpMAPISession, &lpMDB));
		}
	}

	if (SUCCEEDED(hRes) && lpMDB)
	{
		if (!lpszFolderPath.empty())
		{
			hRes = WC_H(HrMAPIOpenFolderExW(lpMDB, lpszFolderPath, &lpFolder));
		}
		else
		{
			lpFolder = mapi::OpenDefaultFolder(ulFolder, lpMDB);
		}
	}

	if (SUCCEEDED(hRes))
	{
		if (lpFolder)
		{
			if (lppFolder)
			{
				*lppFolder = lpFolder;
			}
			else
			{
				lpFolder->Release();
			}
		}

		if (lpMDB)
		{
			if (lppMDB)
			{
				*lppMDB = lpMDB;
			}
			else
			{
				lpMDB->Release();
			}
		}
	}
	else
	{
		if (lpFolder) lpFolder->Release();
		if (lpMDB) lpMDB->Release();
	}

	return hRes;
}

void PrintObjectProperty(_In_ LPMAPIPROP lpMAPIProp, ULONG ulPropTag)
{
	if (!lpMAPIProp || !ulPropTag) return;

	LPSPropValue lpAllProps = nullptr;
	ULONG cValues = 0L;

	SPropTagArray sTag = {0};
	sTag.cValues = 1;
	sTag.aulPropTag[0] = ulPropTag;

	WC_H_GETPROPS(lpMAPIProp->GetProps(&sTag, fMapiUnicode, &cValues, &lpAllProps));

	output::_OutputProperties(DBGNoDebug, stdout, cValues, lpAllProps, lpMAPIProp, true);

	MAPIFreeBuffer(lpAllProps);
}

void PrintObjectProperties(const std::wstring& szObjType, _In_ LPMAPIPROP lpMAPIProp, ULONG ulPropTag)
{
	auto hRes = S_OK;
	if (!lpMAPIProp) return;

	wprintf(output::g_szXMLHeader.c_str());
	wprintf(L"<%ws>\n", szObjType.c_str());

	LPSPropValue lpAllProps = nullptr;
	ULONG cValues = 0L;

	if (ulPropTag)
	{
		SPropTagArray sTag = {0};
		sTag.cValues = 1;
		sTag.aulPropTag[0] = ulPropTag;

		hRes = WC_H_GETPROPS(lpMAPIProp->GetProps(&sTag, fMapiUnicode, &cValues, &lpAllProps));
	}
	else
	{
		hRes = WC_H_GETPROPS(mapi::GetPropsNULL(lpMAPIProp, fMapiUnicode, &cValues, &lpAllProps));
	}

	if (FAILED(hRes))
	{
		wprintf(L"<properties error=\"0x%08X\" />\n", hRes);
	}
	else if (lpAllProps)
	{
		wprintf(L"<properties>\n");

		output::_OutputProperties(DBGNoDebug, stdout, cValues, lpAllProps, lpMAPIProp, true);

		wprintf(L"</properties>\n");

		MAPIFreeBuffer(lpAllProps);
	}

	wprintf(L"</%ws>\n", szObjType.c_str());
}

void PrintStoreProperty(_In_ LPMAPISESSION lpMAPISession, ULONG ulIndex, ULONG ulPropTag)
{
	if (!lpMAPISession || !ulPropTag) return;

	LPMDB lpMDB = nullptr;
	WC_H_S(OpenStore(lpMAPISession, ulIndex, &lpMDB));
	if (lpMDB)
	{
		PrintObjectProperty(lpMDB, ulPropTag);

		lpMDB->Release();
	}
}

void PrintStoreProperties(_In_ LPMDB lpMDB, ULONG ulPropTag)
{
	PrintObjectProperties(L"messagestoreprops", lpMDB, ulPropTag);
}

void PrintStoreTable(_In_ LPMAPISESSION lpMAPISession, ULONG ulPropTag)
{
	auto hRes = S_OK;
	LPMAPITABLE lpStoreTable = nullptr;
	if (!lpMAPISession) return;

	wprintf(output::g_szXMLHeader.c_str());
	wprintf(L"<storetable>\n");
	WC_MAPI(lpMAPISession->GetMsgStoresTable(0, &lpStoreTable));

	if (lpStoreTable)
	{
		auto sTags = LPSPropTagArray(&columns::sptSTORECols);
		SPropTagArray sTag = {0};
		if (ulPropTag)
		{
			sTag.cValues = 1;
			sTag.aulPropTag[0] = ulPropTag;
			sTags = &sTag;
		}

		WC_MAPI(lpStoreTable->SetColumns(sTags, TBL_ASYNC));

		if (SUCCEEDED(hRes))
		{
			LPSRowSet lpRows = nullptr;
			ULONG iCurStore = 0;
			if (!FAILED(hRes))
				for (;;)
				{
					hRes = S_OK;
					if (lpRows) FreeProws(lpRows);
					lpRows = nullptr;
					WC_MAPI(lpStoreTable->QueryRows(10, NULL, &lpRows));
					if (FAILED(hRes) || !lpRows || !lpRows->cRows) break;

					for (ULONG i = 0; i < lpRows->cRows; i++)
					{
						wprintf(L"<properties index=\"%u\">\n", iCurStore);
						if (ulPropTag && lpRows->aRow[0].lpProps &&
							PT_ERROR == PROP_TYPE(lpRows->aRow[0].lpProps->ulPropTag) &&
							MAPI_E_NOT_FOUND == lpRows->aRow[0].lpProps->Value.err)
						{
							PrintStoreProperty(lpMAPISession, i, ulPropTag);
						}
						else
						{
							output::_OutputProperties(
								DBGNoDebug, stdout, lpRows->aRow[i].cValues, lpRows->aRow[i].lpProps, nullptr, false);
						}

						wprintf(L"</properties>\n");
						iCurStore++;
					}
				}

			if (lpRows) FreeProws(lpRows);
		}
	}

	wprintf(L"</storetable>\n");
	if (lpStoreTable) lpStoreTable->Release();
}

void DoStore(_In_ MYOPTIONS ProgOpts)
{
	ULONG ulPropTag = NULL;

	// If we have a prop tag, parse it
	// For now, we don't support dispids
	if (!ProgOpts.lpszUnswitchedOption.empty() && !(ProgOpts.ulOptions & OPT_DODISPID))
	{
		ulPropTag = interpretprop::PropNameToPropTag(ProgOpts.lpszUnswitchedOption);
	}

	LPMDB lpMDB = nullptr;
	if (ProgOpts.lpMAPISession)
	{
		if (0 == ProgOpts.ulStore)
		{
			PrintStoreTable(ProgOpts.lpMAPISession, ulPropTag);
		}
		else
		{
			// ulStore was incremented by 1 before, so drop it back now
			WC_H_S(OpenStore(ProgOpts.lpMAPISession, ProgOpts.ulStore - 1, &lpMDB));
		}
	}

	if (lpMDB)
	{
		PrintStoreProperties(lpMDB, ulPropTag);
		lpMDB->Release();
	}
}