#include <windows.h>
#include <CommCtrl.h>
#include <Shlwapi.h>
#include "nsiswapi\pluginapi.h"


//++ IsWow64
BOOL IsWow64()
{
	BOOL bIsWow64 = FALSE;

	typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
	LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress( GetModuleHandle( _T("kernel32")), "IsWow64Process" );

	if ( fnIsWow64Process && fnIsWow64Process( GetCurrentProcess(), &bIsWow64 ))
		return bIsWow64;

	return FALSE;
}

//++ EnableWow64FsRedirection
BOOLEAN EnableWow64FsRedirection( __in BOOLEAN bEnable )
{
	BOOL bRet = FALSE;

	typedef BOOLEAN (WINAPI *LPFN_WOW64EW64FSR)(BOOLEAN);
	LPFN_WOW64EW64FSR fnEnableFsRedir = (LPFN_WOW64EW64FSR)GetProcAddress( GetModuleHandle( _T("kernel32")), "Wow64EnableWow64FsRedirection" );

	if ( fnEnableFsRedir )
		bRet = fnEnableFsRedir( bEnable );

	return bRet;
}


//++ Subclassing definitions
#define PROP_WNDPROC_OLD				_T("NSutils.WndProc.Old")
#define PROP_WNDPROC_REFCOUNT			_T("NSutils.WndProc.RefCount")
#define PROP_PROGRESSBAR_NOSTEPBACK		_T("NSutils.ProgressBar.NoStepBack")
#define PROP_PROGRESSBAR_REDIRECTWND	_T("NSutils.ProgressBar.RedirectWnd")

//++ MySubclassWindow
INT_PTR MySubclassWindow(
	__in HWND hWnd,
	__in WNDPROC fnWndProc
	)
{
	INT_PTR iRefCount = 0;

	BOOLEAN bSubclassed = FALSE;
	if ( GetProp( hWnd, PROP_WNDPROC_OLD )) {
		/// Already subclassed
		bSubclassed = TRUE;
	} else {
		/// Subclass now
		WNDPROC fnOldWndProc = (WNDPROC)SetWindowLongPtr( hWnd, GWLP_WNDPROC, (LONG_PTR)fnWndProc );
		if ( fnOldWndProc ) {
			SetProp( hWnd, PROP_WNDPROC_OLD, (HANDLE)fnOldWndProc );
			bSubclassed = TRUE;
		}
	}

	// Update the reference count
	if ( bSubclassed ) {
		iRefCount = (INT_PTR)GetProp( hWnd, PROP_WNDPROC_REFCOUNT );
		iRefCount++;
		SetProp( hWnd, PROP_WNDPROC_REFCOUNT, (HANDLE)iRefCount );
	}

	return iRefCount;
}


//++ MyUnsubclassWindow
INT_PTR MyUnsubclassWindow(
	__in HWND hWnd
	)
{
	INT_PTR iRefCount = 0;

	WNDPROC fnOldWndProc = (WNDPROC)GetProp( hWnd, PROP_WNDPROC_OLD );
	if ( fnOldWndProc ) {

		// Unsubclass the window if there are no other active timers (check the refcount)
		iRefCount = (INT_PTR)GetProp( hWnd, PROP_WNDPROC_REFCOUNT );
		iRefCount--;
		if ( iRefCount <= 0 ) {
			/// Unsubclass the window
			SetWindowLongPtr( hWnd, GWLP_WNDPROC, (LONG_PTR)fnOldWndProc );
			RemoveProp( hWnd, PROP_WNDPROC_OLD );
			RemoveProp( hWnd, PROP_WNDPROC_REFCOUNT );
		} else {
			/// Decrement the refcount
			SetProp( hWnd, PROP_WNDPROC_REFCOUNT, (HANDLE)iRefCount );
		}
	}

	return iRefCount;
}


/***
*memmove - Copy source buffer to destination buffer
*
*Purpose:
*       memmove() copies a source memory buffer to a destination memory buffer.
*       This routine recognize overlapping buffers to avoid propagation.
*       For cases where propogation is not a problem, memcpy() can be used.
*
*Entry:
*       void *dst = pointer to destination buffer
*       const void *src = pointer to source buffer
*       size_t count = number of bytes to copy
*
*Exit:
*       Returns a pointer to the destination buffer
*
*Exceptions:
*******************************************************************************/

void * __cdecl memmove (
        void * dst,
        const void * src,
        size_t count
        )
{
        void * ret = dst;

#if defined (_M_X64)

        {


        __declspec(dllimport)


        void RtlMoveMemory( void *, const void *, size_t count );

        RtlMoveMemory( dst, src, count );

        }

#else  /* defined (_M_X64) */
        if (dst <= src || (char *)dst >= ((char *)src + count)) {
                /*
                 * Non-Overlapping Buffers
                 * copy from lower addresses to higher addresses
                 */
                while (count--) {
                        *(char *)dst = *(char *)src;
                        dst = (char *)dst + 1;
                        src = (char *)src + 1;
                }
        }
        else {
                /*
                 * Overlapping Buffers
                 * copy from higher addresses to lower addresses
                 */
                dst = (char *)dst + count - 1;
                src = (char *)src + count - 1;

                while (count--) {
                        *(char *)dst = *(char *)src;
                        dst = (char *)dst - 1;
                        src = (char *)src - 1;
                }
        }
#endif  /* defined (_M_X64) */

        return(ret);
}


//++ ExecutePendingFileRenameOperationsImpl
DWORD ExecutePendingFileRenameOperationsImpl(
	__in_opt LPCTSTR pszSrcFileSubstr,		/// Case insensitive. Only SrcFile-s that contain this substring will be processed. Can be empty in which case all pended file operations are executed.
	__out_opt LPDWORD pdwFileOpError
	)
{
#define REGKEY_PENDING_FILE_OPS	_T("SYSTEM\\CurrentControlSet\\Control\\Session Manager")
#define REGVAL_PENDING_FILE_OPS _T("PendingFileRenameOperations")

	DWORD err = ERROR_SUCCESS, err2, err3;
	HKEY hKey;
	BYTE iMajorVersion = LOBYTE(LOWORD(GetVersion()));
	BYTE iMinorVersion = HIBYTE(LOWORD(GetVersion()));
	DWORD dwKeyFlags = 0;
	BOOLEAN bDisabledFSRedirection = FALSE;

	if ( iMajorVersion > 5 || ( iMajorVersion == 5 && iMinorVersion >= 1 ))		/// XP or newer
		dwKeyFlags |= KEY_WOW64_64KEY;

	if ( pdwFileOpError )
		*pdwFileOpError = ERROR_SUCCESS;

	// Read the REG_MULTI_SZ value
	err = RegOpenKeyEx( HKEY_LOCAL_MACHINE, REGKEY_PENDING_FILE_OPS, 0, KEY_READ | KEY_WRITE | dwKeyFlags, &hKey );
	if ( err == ERROR_SUCCESS ) {
		DWORD dwType, dwSize = 0;
		err = RegQueryValueEx( hKey, REGVAL_PENDING_FILE_OPS, NULL, &dwType, NULL, &dwSize );
		if ( err == ERROR_SUCCESS && dwType == REG_MULTI_SZ && dwSize > 0 ) {
			LPTSTR pszValue = (LPTSTR)GlobalAlloc( GMEM_FIXED, dwSize );
			if ( pszValue ) {
				err = RegQueryValueEx( hKey, REGVAL_PENDING_FILE_OPS, NULL, &dwType, (LPBYTE)pszValue, &dwSize );
				if ( err == ERROR_SUCCESS && dwType == REG_MULTI_SZ && dwSize > 0 ) {

					// PendingFileRenameOperations contains pairs of strings (SrcFile, DstFile)
					// At boot time every SrcFile is renamed to DstFile.
					// If DstFile is empty, SrcFile is deleted.
					// (DstFile might start with "!", not sure why...)

					int i;
					int iLen = (int)(dwSize / sizeof(TCHAR));	/// Registry value length in TCHAR-s
					int iIndexSrcFile, iIndexDstFile, iNextIndex;
					BOOLEAN bValueDirty = FALSE;

					for ( i = 0, iIndexSrcFile = 0, iIndexDstFile = -1; i < iLen; i++ ) {

						if ( pszValue[i] == _T('\0')) {

							if ( iIndexDstFile == -1 ) {

								// At this point we have SrcFile
								iIndexDstFile = i + 1;	/// Remember where DstFile begins

							} else {

								// At this point we have both, SrcFile and DstFile
								// We'll execute the pended operation if SrcFile matches our search criteria

								if ( pszValue[iIndexSrcFile] &&
									( !pszSrcFileSubstr || !*pszSrcFileSubstr || StrStrI( pszValue + iIndexSrcFile, pszSrcFileSubstr ))
									)
								{
									/// Ignore "\??\" prefix
									LPCTSTR pszSrcFile = pszValue + iIndexSrcFile;
									if ( StrCmpN( pszSrcFile, _T("\\??\\"), 4 ) == 0 )
										pszSrcFile += 4;

									/// Disable file system redirection (Vista+)
									if ( !bDisabledFSRedirection && IsWow64())
										bDisabledFSRedirection = EnableWow64FsRedirection( FALSE );

									if ( !pszValue[iIndexDstFile] ) {

										// Delete SrcFile
										err3 = ERROR_SUCCESS;
										if ( !DeleteFile( pszSrcFile )) {
											err3 = err2 = GetLastError();
											if ((err2 == ERROR_FILE_NOT_FOUND) || (err2 == ERROR_INVALID_NAME) || (err2 == ERROR_PATH_NOT_FOUND) || (err2 == ERROR_INVALID_DRIVE))
												err2 = ERROR_SUCCESS;	/// Forget errors for files that don't exist
											if ( pdwFileOpError && ( *pdwFileOpError == ERROR_SUCCESS ))	/// Only the first encountered error is remembered
												*pdwFileOpError = err2;
										}

										/*{
											TCHAR sz[512];
											wsprintf( sz, _T("Delete( \"%s\" ) == 0x%x\n"), pszSrcFile, err3 );
											OutputDebugString( sz );
										}*/

									} else {

										/// Ignore "!" and "\??\" prefixes
										LPCTSTR pszDstFile = pszValue + iIndexDstFile;
										if ( *pszDstFile == _T('!'))
											pszDstFile++;
										if ( StrCmpN( pszDstFile, _T("\\??\\"), 4 ) == 0 )
											pszDstFile += 4;

										// Rename SrcFile -> DstFile
										err3 = ERROR_SUCCESS;
										if ( !MoveFileEx( pszSrcFile, pszDstFile, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED )) {
											err3 = err2 = GetLastError();
											if ((err2 == ERROR_FILE_NOT_FOUND) || (err2 == ERROR_INVALID_NAME) || (err2 == ERROR_PATH_NOT_FOUND) || (err2 == ERROR_INVALID_DRIVE))
												err2 = ERROR_SUCCESS;	/// Forget errors for files that don't exist
											if ( pdwFileOpError && ( *pdwFileOpError == ERROR_SUCCESS ))	/// Only the first encountered error is remembered
												*pdwFileOpError = err2;
										}

										/*{
											TCHAR sz[512];
											wsprintf( sz, _T("Rename( \"%s\", \"%s\" ) == 0x%x\n"), pszSrcFile, pszDstFile, err3 );
											OutputDebugString( sz );
										}*/
									}

									// Remove the current pended operation from memory
									iNextIndex = i + 1;
									MoveMemory( pszValue + iIndexSrcFile, pszValue + iNextIndex, (iLen - iNextIndex) * sizeof(TCHAR));
									iLen -= (iNextIndex - iIndexSrcFile);
									dwSize -= (iNextIndex - iIndexSrcFile) * sizeof(TCHAR);
									bValueDirty = TRUE;

									// Next string index
									i = iIndexSrcFile - 1;
									iIndexDstFile = -1;

								} else {
									iIndexSrcFile = i + 1;
									iIndexDstFile = -1;
								}
							}
						}
					}

					// Write the new PendingFileRenameOperations value.
					// Pended operations that we've executed were removed from the list.
					if ( bValueDirty ) {
						if ( dwSize <= 2 ) {
							err = RegDeleteValue( hKey, REGVAL_PENDING_FILE_OPS );
						} else {
							err = RegSetValueEx( hKey, REGVAL_PENDING_FILE_OPS, 0, REG_MULTI_SZ, (LPBYTE)pszValue, dwSize );
						}
					}
				}

				GlobalFree( pszValue );

			} else {
				err = ERROR_OUTOFMEMORY;
			}
		}
		RegCloseKey( hKey );
	}

	/// Re-enable file system redirection (Vista+)
	if ( bDisabledFSRedirection )
		EnableWow64FsRedirection( TRUE );

	return err;
}


//
//  [exported] ExecutePendingFileRenameOperations
//  ----------------------------------------------------------------------
//  Example:
//    NSutils::ExecutePendingFileRenameOperations "SrcFileSubstr"
//    Pop $0 ; Win32 error code
//    Pop $1 ; File operations Win32 error code
//    ${If} $0 = 0
//      ;Success
//    ${Else}
//      ;Error
//    ${EndIf}
//
void __declspec(dllexport) ExecutePendingFileRenameOperations(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	LPTSTR pszBuf = NULL;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters
	/// Allocate memory large enough to store any NSIS string
	pszBuf = (TCHAR*)GlobalAlloc( GPTR, sizeof(TCHAR) * string_size );
	if ( pszBuf ) {

		///	Param1: SrcFileSubstr
		if ( popstring( pszBuf ) == 0 ) {

			DWORD err, fileop_err;
			err = ExecutePendingFileRenameOperationsImpl( pszBuf, &fileop_err );

			wsprintf( pszBuf, _T("%hu"), fileop_err );
			pushstring( pszBuf );

			wsprintf( pszBuf, _T("%hu"), err );
			pushstring( pszBuf );
		}

		/// Free memory
		GlobalFree( pszBuf );
	}
}


//++ ProgressBarWndProc
LRESULT CALLBACK ProgressBarWndProc(
	__in HWND hWnd,
	__in UINT iMsg,
	__in WPARAM wParam,
	__in LPARAM lParam
	)
{
	WNDPROC fnOriginalWndProc = (WNDPROC)GetProp( hWnd, PROP_WNDPROC_OLD );

	switch ( iMsg )
	{
		case PBM_SETPOS:
			{
				// Prevent stepping back?
				if ( GetProp( hWnd, PROP_PROGRESSBAR_NOSTEPBACK ) == (HANDLE)TRUE )
				{
					int iNewPos = (int)wParam;
					int iCurPos = (int)SendMessage( hWnd, PBM_GETPOS, 0, 0 );

					// We don't allow the progress bar to go backward
					// ...except when set to zero
					if (( iNewPos <= iCurPos ) && ( iNewPos > 0 )) {
						return iCurPos;
					}
				}

				// Redirect the message to the second progress bar, if set
				if ( TRUE ) {
					HWND hProgressBar2 = (HWND)GetProp( hWnd, PROP_PROGRESSBAR_REDIRECTWND );
					if ( hProgressBar2 )
						SendMessage( hProgressBar2, iMsg, wParam, lParam );
				}
				break;
			}

		case PBM_SETRANGE:
		case PBM_DELTAPOS:
		case PBM_SETSTEP:
		case PBM_STEPIT:
		case PBM_SETRANGE32:
		case PBM_SETBARCOLOR:
		case PBM_SETBKCOLOR:
		case PBM_SETMARQUEE:
		case PBM_SETSTATE:
			{
				// Redirect the message to the second progress bar, if set
				HWND hProgressBar2 = (HWND)GetProp( hWnd, PROP_PROGRESSBAR_REDIRECTWND );
				if ( hProgressBar2 )
					SendMessage( hProgressBar2, iMsg, wParam, lParam );
				break;
			}

		case WM_DESTROY:
			{
				// Unsubclass this window. Should have been done by the caller...
				while ( MyUnsubclassWindow( hWnd ) > 0 );
				RemoveProp( hWnd, PROP_PROGRESSBAR_NOSTEPBACK );
				RemoveProp( hWnd, PROP_PROGRESSBAR_REDIRECTWND );
				break;
			}
	}

	// Call the original WndProc
	if ( fnOriginalWndProc ) {
		return CallWindowProc( fnOriginalWndProc, hWnd, iMsg, wParam, lParam );
	} else {
		return DefWindowProc( hWnd, iMsg, wParam, lParam );
	}
}


//
//  [exported] DisableProgressStepBack
//  ----------------------------------------------------------------------
//  Input:  Progress bar window handle
//  Output: None
//
//  Example:
//    NSutils::DisableProgressStepBack /NOUNLOAD $mui.InstFilesPage.ProgressBar
//    /NOUNLOAD is mandatory for obvious reasons...
void __declspec(dllexport) DisableProgressStepBack(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	HWND hProgressBar;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters
	///	Param1: Progress bar handle
	hProgressBar = (HWND)popint();
	if ( hProgressBar && IsWindow( hProgressBar )) {

		if ( GetProp( hProgressBar, PROP_PROGRESSBAR_NOSTEPBACK ) == NULL ) {	/// Already set?
			if ( MySubclassWindow( hProgressBar, ProgressBarWndProc ) > 0 ) {
				SetProp( hProgressBar, PROP_PROGRESSBAR_NOSTEPBACK, (HANDLE)TRUE );
			}
		}
	}
}


//
//  [exported] RestoreProgressStepBack
//  ----------------------------------------------------------------------
//  Input:  Progress bar window handle
//  Output: None
//
//  Example:
//    NSutils::RestoreProgressStepBack /NOUNLOAD $mui.InstFilesPage.ProgressBar
//
void __declspec(dllexport) RestoreProgressStepBack(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	HWND hProgressBar;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters
	///	Param1: Progress bar handle
	hProgressBar = (HWND)popint();
	if ( hProgressBar && IsWindow( hProgressBar )) {

		if ( GetProp( hProgressBar, PROP_PROGRESSBAR_NOSTEPBACK ) != NULL ) {	/// Ever set?
			RemoveProp( hProgressBar, PROP_PROGRESSBAR_NOSTEPBACK );
			MyUnsubclassWindow( hProgressBar );
		}
	}
}


//
//  [exported] RedirectProgressBar
//  ----------------------------------------------------------------------
//  Input:  ProgressBarWnd SecondProgressBarWnd
//  Output: None
//
//  Example:
//    NSutils::RedirectProgressBar /NOUNLOAD $mui.InstFilesPage.ProgressBar $mui.MyProgressBar
//
void __declspec(dllexport) RedirectProgressBar(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	HWND hProgressBar, hProgressBar2;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters

	///	Param1: Progress bar handle
	hProgressBar = (HWND)popint();

	///	Param2: Second progress bar handle. If NULL, the redirection is canceled
	hProgressBar2 = (HWND)popint();

	if ( hProgressBar && IsWindow( hProgressBar )) {

		if ( hProgressBar2 ) {

			// Activate progress bar message redirection
			if ( GetProp( hProgressBar, PROP_PROGRESSBAR_REDIRECTWND ) == NULL ) {
				/// New redirection
				if ( MySubclassWindow( hProgressBar, ProgressBarWndProc ) > 1 ) {
					SetProp( hProgressBar, PROP_PROGRESSBAR_REDIRECTWND, hProgressBar2 );
				}
			} else {
				/// Update existing redirection
				SetProp( hProgressBar, PROP_PROGRESSBAR_REDIRECTWND, hProgressBar2 );
			}

			// Clone the characteristics of the first progress bar to the second one
			SetWindowLongPtr( hProgressBar2, GWL_STYLE, GetWindowLongPtr( hProgressBar, GWL_STYLE ));
			SetWindowLongPtr( hProgressBar2, GWL_EXSTYLE, GetWindowLongPtr( hProgressBar, GWL_EXSTYLE ));
			SendMessage(
				hProgressBar2,
				PBM_SETRANGE32,
				SendMessage( hProgressBar, PBM_GETRANGE, TRUE, 0 ),		/// Low limit
				SendMessage( hProgressBar, PBM_GETRANGE, FALSE, 0 )		/// High limit
				);

		} else {

			// Cancel progress bar message redirection
			if ( GetProp( hProgressBar, PROP_PROGRESSBAR_REDIRECTWND ) != NULL ) {
				RemoveProp( hProgressBar, PROP_PROGRESSBAR_REDIRECTWND );
				MyUnsubclassWindow( hProgressBar );
			}
		}
	}
}


//++ MainWndProc
LRESULT CALLBACK MainWndProc(
	__in HWND hWnd,
	__in UINT iMsg,
	__in WPARAM wParam,
	__in LPARAM lParam
	)
{
	WNDPROC fnOriginalWndProc = (WNDPROC)GetProp( hWnd, PROP_WNDPROC_OLD );

	switch ( iMsg )
	{
	case WM_TIMER:
		{
			// The NSIS callback is also used as timer ID
			int iNsisCallback = (int)wParam;

			// Call the NSIS callback
			g_ep->ExecuteCodeSegment( iNsisCallback - 1, 0 );

			break;
		}

	case WM_DESTROY:
		{
			// Unsubclass this window. Should have been done by the caller...
			while ( MyUnsubclassWindow( hWnd ) > 0 );
			break;
		}
	}

	// Call the original WndProc
	if ( fnOriginalWndProc ) {
		return CallWindowProc( fnOriginalWndProc, hWnd, iMsg, wParam, lParam );
	} else {
		return DefWindowProc( hWnd, iMsg, wParam, lParam );
	}
}

//
//  [exported] StartTimer
//  ----------------------------------------------------------------------
//  Input:  NsisCallbackFunction TimerInterval
//  Output: None
//
//  The NsisCallbackFunction is a regular NSIS function, no input, no output.
//  TimerInterval in milliseconds (1000ms = 1s)
//
//  Example:
//    GetFunctionAddress $0 OnMyTimer
//    NSutils::StartTimer /NOUNLOAD $0 1000
//
void __declspec(dllexport) StartTimer(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	int iCallback;
	int iPeriod;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters

	///	Param1: Callback function
	iCallback = popint();

	/// Param2: Timer interval (milliseconds)
	iPeriod = popint();

	// SetTimer
	if (( iCallback != 0 ) && ( iPeriod > 0 ) && hWndParent && IsWindow( hWndParent )) {

		if ( MySubclassWindow( hWndParent, MainWndProc ) > 0 ) {

			/// Start the timer
			/// Use the NSIS callback as timer ID
			SetTimer( hWndParent, iCallback, iPeriod, NULL );
		}
	}
}


//
//  [exported] StopTimer
//  ----------------------------------------------------------------------
//  Input:  NsisCallbackFunction
//  Output: None
//
//  Example:
//    GetFunctionAddress $0 OnMyTimer
//    NSutils::StopTimer $0
//
void __declspec(dllexport) StopTimer(
	HWND hWndParent,
	int string_size,
	TCHAR *variables,
	stack_t **stacktop,
	extra_parameters *extra
	)
{
	int iCallback;

	//	Cache global structures
	EXDLL_INIT();

	//	Check NSIS API compatibility
	if ( !IsCompatibleApiVersion()) {
		/// TODO: display an error message?
		return;
	}

	//	Retrieve NSIS parameters

	///	Param1: Callback function
	iCallback = popint();

	// Kill the timer
	if (( iCallback != 0 ) && hWndParent && IsWindow( hWndParent )) {

		KillTimer( hWndParent, iCallback );
		MyUnsubclassWindow( hWndParent );
	}
}
