#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef DWORD FT_STATUS;
static const FT_STATUS FT_OK=0, FT_INVALID_HANDLE=1, FT_DEVICE_NOT_FOUND=2, FT_DEVICE_NOT_OPENED=3, FT_IO_ERROR=4, FT_INVALID_PARAMETER=6;
static CRITICAL_SECTION lock; static INIT_ONCE once=INIT_ONCE_STATIC_INIT; static HANDLE port=INVALID_HANDLE_VALUE;
BOOL CALLBACK init(PINIT_ONCE,PVOID,PVOID*){InitializeCriticalSectionAndSpinCount(&lock,4000);return TRUE;}
static void enter(){InitOnceExecuteOnce(&once,init,0,0);EnterCriticalSection(&lock);} static void leave(){LeaveCriticalSection(&lock);}
static bool valid(PVOID h){return port!=INVALID_HANDLE_VALUE && h==(PVOID)port;}
static bool findPort(wchar_t* out,DWORD chars){HKEY k=0;if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DEVICEMAP\\SERIALCOMM",0,KEY_READ,&k)!=ERROR_SUCCESS)return false;DWORD i=0;wchar_t name[256],value[64];bool found=false;for(;;){DWORD nn=256,vn=sizeof(value),type=0;LONG e=RegEnumValueW(k,i++,name,&nn,0,&type,(BYTE*)value,&vn);if(e==ERROR_NO_MORE_ITEMS)break;if(e==ERROR_SUCCESS&&type==REG_SZ&&wcsstr(name,L"VCP")&&wcsncmp(value,L"COM",3)==0){wcsncpy_s(out,chars,L"\\\\.\\",_TRUNCATE);wcsncat_s(out,chars,value,_TRUNCATE);found=true;break;}}RegCloseKey(k);return found;}
static FT_STATUS configure(){DCB d={};d.DCBlength=sizeof(d);if(!GetCommState(port,&d))return FT_IO_ERROR;d.BaudRate=9600;d.ByteSize=8;d.Parity=NOPARITY;d.StopBits=ONESTOPBIT;d.fBinary=TRUE;d.fOutxCtsFlow=TRUE;d.fRtsControl=RTS_CONTROL_HANDSHAKE;d.fOutX=d.fInX=FALSE;if(!SetCommState(port,&d))return FT_IO_ERROR;COMMTIMEOUTS t={MAXDWORD,0,0,0,5000};if(!SetCommTimeouts(port,&t))return FT_IO_ERROR;SetupComm(port,65536,65536);return FT_OK;}
#define EXP extern "C" __declspec(dllexport) FT_STATUS WINAPI
EXP FT_CreateDeviceInfoList(LPDWORD n){if(!n)return FT_INVALID_PARAMETER;wchar_t p[64];*n=findPort(p,64)?1:0;return FT_OK;}
EXP FT_GetDeviceInfoDetail(DWORD i,LPDWORD flags,LPDWORD type,LPDWORD id,LPDWORD loc,LPVOID sn,LPVOID desc,PVOID*h){if(i)return FT_DEVICE_NOT_FOUND;if(flags)*flags=2|(port!=INVALID_HANDLE_VALUE?1:0);if(type)*type=8;if(id)*id=0x04036014;if(loc)*loc=0x13;if(sn){ZeroMemory(sn,16);memcpy(sn,"0001",4);}if(desc){ZeroMemory(desc,64);memcpy(desc,"Haltech Elite",13);}if(h)*h=(port==INVALID_HANDLE_VALUE?0:(PVOID)port);return FT_OK;}
EXP FT_Open(int i,PVOID*h){if(i!=0||!h)return FT_DEVICE_NOT_FOUND;enter();if(port!=INVALID_HANDLE_VALUE){leave();return FT_DEVICE_NOT_OPENED;}wchar_t path[64];if(!findPort(path,64)){leave();return FT_DEVICE_NOT_FOUND;}port=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);if(port==INVALID_HANDLE_VALUE){leave();return FT_DEVICE_NOT_OPENED;}FT_STATUS s=configure();if(s){CloseHandle(port);port=INVALID_HANDLE_VALUE;leave();return s;}*h=(PVOID)port;leave();return FT_OK;}
EXP FT_OpenEx(PVOID arg,DWORD flags,PVOID*h){if(!arg)return FT_INVALID_PARAMETER;if(flags==1&&strcmp((char*)arg,"0001")!=0)return FT_DEVICE_NOT_FOUND;return FT_Open(0,h);}
EXP FT_Close(PVOID h){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}CloseHandle(port);port=INVALID_HANDLE_VALUE;leave();return FT_OK;}
EXP FT_Read(PVOID h,LPVOID b,DWORD n,LPDWORD got){if(!got)return FT_INVALID_PARAMETER;*got=0;enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}BOOL ok=ReadFile(port,b,n,got,0);leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_Write(PVOID h,LPVOID b,DWORD n,LPDWORD put){if(!put)return FT_INVALID_PARAMETER;*put=0;enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}BOOL ok=WriteFile(port,b,n,put,0);leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_GetQueueStatus(PVOID h,LPDWORD n){if(!n)return FT_INVALID_PARAMETER;enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DWORD e=0;COMSTAT s={};BOOL ok=ClearCommError(port,&e,&s);*n=s.cbInQue;leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_GetStatus(PVOID h,LPDWORD rx,LPDWORD tx,LPDWORD ev){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DWORD e=0;COMSTAT s={};BOOL ok=ClearCommError(port,&e,&s);if(rx)*rx=s.cbInQue;if(tx)*tx=s.cbOutQue;if(ev)*ev=0;leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_Purge(PVOID h,DWORD mask){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DWORD f=0;if(mask&1)f|=PURGE_RXABORT|PURGE_RXCLEAR;if(mask&2)f|=PURGE_TXABORT|PURGE_TXCLEAR;BOOL ok=PurgeComm(port,f);leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_SetBaudRate(PVOID h,DWORD baud){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DCB d={};d.DCBlength=sizeof(d);BOOL ok=GetCommState(port,&d);if(ok){d.BaudRate=baud;ok=SetCommState(port,&d);}leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_SetDataCharacteristics(PVOID h,UCHAR bits,UCHAR stop,UCHAR parity){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DCB d={};d.DCBlength=sizeof(d);BOOL ok=GetCommState(port,&d);if(ok){d.ByteSize=bits;d.StopBits=(stop==2?TWOSTOPBITS:ONESTOPBIT);d.Parity=(BYTE)parity;ok=SetCommState(port,&d);}leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_SetFlowControl(PVOID h,USHORT flow,UCHAR,UCHAR){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}DCB d={};d.DCBlength=sizeof(d);BOOL ok=GetCommState(port,&d);if(ok){d.fOutxCtsFlow=(flow==0x100);d.fRtsControl=(flow==0x100?RTS_CONTROL_HANDSHAKE:RTS_CONTROL_ENABLE);d.fOutX=d.fInX=(flow==0x400);ok=SetCommState(port,&d);}leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_SetTimeouts(PVOID h,DWORD r,DWORD w){enter();if(!valid(h)){leave();return FT_INVALID_HANDLE;}COMMTIMEOUTS t={MAXDWORD,0,r,0,w};BOOL ok=SetCommTimeouts(port,&t);leave();return ok?FT_OK:FT_IO_ERROR;}
EXP FT_SetBitMode(PVOID h,UCHAR,UCHAR mode){return valid(h)&&mode==0?FT_OK:(valid(h)?FT_INVALID_PARAMETER:FT_INVALID_HANDLE);}
EXP FT_SetLatencyTimer(PVOID h,UCHAR){return valid(h)?FT_OK:FT_INVALID_HANDLE;}
EXP FT_SetUSBParameters(PVOID h,DWORD in,DWORD out){if(!valid(h))return FT_INVALID_HANDLE;SetupComm(port,in,out);return FT_OK;}
EXP FT_ResetDevice(PVOID h){if(!valid(h))return FT_INVALID_HANDLE;PurgeComm(port,PURGE_RXABORT|PURGE_RXCLEAR|PURGE_TXABORT|PURGE_TXCLEAR);return FT_OK;}
EXP FT_GetLibraryVersion(LPDWORD v){if(v)*v=0x00040000;return FT_OK;}
EXP FT_GetDriverVersion(PVOID h,LPDWORD v){if(!valid(h))return FT_INVALID_HANDLE;if(v)*v=0x02123620;return FT_OK;}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){if(reason==DLL_PROCESS_DETACH&&port!=INVALID_HANDLE_VALUE)CloseHandle(port);return TRUE;}
