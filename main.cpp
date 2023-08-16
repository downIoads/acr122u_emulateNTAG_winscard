#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winscard.h>

typedef struct _SCARD_DUAL_HANDLE {
	SCARDCONTEXT hContext;
	SCARDHANDLE hCard;
} SCARD_DUAL_HANDLE, * PSCARD_DUAL_HANDLE;


void PrintHex(LPCBYTE pbData, DWORD cbData)
{
	DWORD i;
	for (i = 0; i < cbData; i++)
	{
		wprintf(L"%02x ", pbData[i]);
	}
	wprintf(L"\n");
}

BOOL SendRecvReader(PSCARD_DUAL_HANDLE pHandle, const BYTE* pbData, const UINT16 cbData, BYTE* pbResult, UINT16* pcbResult)
{
	BOOL status = FALSE;
	DWORD cbRecvLenght = *pcbResult;
	LONG scStatus;

	wprintf(L"> ");
	PrintHex(pbData, cbData);

	scStatus = SCardTransmit(pHandle->hCard, NULL, pbData, cbData, NULL, pbResult, &cbRecvLenght);
	if (scStatus == SCARD_S_SUCCESS)
	{
		*pcbResult = (UINT16)cbRecvLenght;

		wprintf(L"< ");
		PrintHex(pbResult, *pcbResult);

		status = TRUE;
	}
	else wprintf(L"%08x\n", scStatus);

	return status;
}

BOOL OpenReader(LPCWSTR szReaderName, PSCARD_DUAL_HANDLE pHandle)
{
	BOOL status = FALSE;
	LONG scStatus;
	DWORD dwActiveProtocol;

	scStatus = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &pHandle->hContext);
	if (scStatus == SCARD_S_SUCCESS)
	{
		scStatus = SCardConnect(pHandle->hContext, szReaderName, SCARD_SHARE_SHARED, SCARD_PROTOCOL_Tx, &pHandle->hCard, &dwActiveProtocol);
		if (scStatus == SCARD_S_SUCCESS)
		{
			status = TRUE;
		}
		else
		{
			SCardReleaseContext(pHandle->hContext);
		}
	}

	return status;
}

void CloseReader(PSCARD_DUAL_HANDLE pHandle)
{
	SCardDisconnect(pHandle->hCard, SCARD_LEAVE_CARD);
	SCardReleaseContext(pHandle->hContext);
}


int EmulateNTag() {

	SCARD_DUAL_HANDLE hDual;
	BYTE Buffer[128];	// TODO figure out smallest value that works
	UINT16 cbBuffer;	// will be updated depending on expected response length in bytes
	
	// my firmware: ACR122U216 (216 is firmware)

	// my Laptop:	"ACS ACR122U PICC Interface 0"
	// my PC:		"ACS ACR122 0"
	if (OpenReader(L"ACS ACR122 0", &hDual))
	{
		// BTW: to send commands directly to PN532 inside ACR122U you must use "ff 00 00 00 <lengthOfPayload> <payload>"

							// 47 00 01
		// 1. read register (returns 7 byte: D5 07 xx yy zz 90 00 )
		const BYTE APDU_Command1[] = { 0xff, 0x00, 0x00, 0x00, 0x08, 0xd4, 0x06, 0x63, 0x05, 0x63, 0x0d, 0x63, 0x38 };

		cbBuffer = 7;	// 7 byte reply expected
		SendRecvReader(&hDual, APDU_Command1, sizeof(APDU_Command1), Buffer, &cbBuffer);	// execute command

		// make sure this operation was successful, terminate if not
		if (!(Buffer[5] == 0x90 && Buffer[6] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Register has successfully been read.\n");
		}

		// 2. update register values
		/*
			xx = xx | 0x04;  // CIU_TxAuto		|= InitialRFOn
			yy = yy & 0xEF;  // CIU_ManualRCV	&= ~ParityDisable
			zz = zz & 0xF7;  // CIU_Status2		&= ~MFCrypto1On
		*/

		// so if xx=47, yy=00, zz=01 then results will be	xx = 47 | 04 = 01000111 | 00000100 = 01000111 = 0x47
		//							yy = 00 & EF = 00000000 & 11101111 = 00000000 = 0x00
		//							zz = 01 & F7 = 00000001 & 11110111 = 00000001 = 0x01

		// 3. write register						  																		       XX				 YY				   ZZ
		const BYTE APDU_Command3[] = { 0xff, 0x00, 0x00, 0x00, 0x11, 0xd4, 0x08, 0x63, 0x02, 0x80, 0x63, 0x03, 0x80, 0x63, 0x05, 0x47, 0x63, 0x0d, 0x00, 0x63, 0x38, 0x01 };
		cbBuffer = 4;	// 4 byte reply expected
		SendRecvReader(&hDual, APDU_Command3, sizeof(APDU_Command3), Buffer, &cbBuffer);	// execute command

		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0xd5 && Buffer[1] == 0x09 && Buffer[2] == 0x90 && Buffer[3] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Register has successfully been written to.\n");
		}

		// 4. set parameters
		const BYTE APDU_Command4[] = { 0xff, 0x00, 0x00, 0x00, 0x03, 0xd4, 0x12, 0x30 };
		cbBuffer = 4;	// 4 byte reply expected
		SendRecvReader(&hDual, APDU_Command4, sizeof(APDU_Command4), Buffer, &cbBuffer);	// execute command

		// make sure this operation was successful, terminate if not
		if (!(Buffer[0] == 0xd5 && Buffer[1] == 0x13 && Buffer[2] == 0x90 && Buffer[3] == 0x00)) {
			CloseReader(&hDual);
			wprintf(L"Error code received. Aborting..\n");
			return 1;
		}
		else {
			wprintf(L"Parameters have successfully been set.\n");
		}
		// 5. TgInitAsTarget
		//  								               ??    ??                            ??...
		// const BYTE APDU_Command5[] =  { 0xff, 0x00, 0x00, 0x00, 0x27, 0xd4, 0x8c, 0x00, 0x08, 0x00, 0x12, 0x34, 0x56, 0x40, 0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xFF, 0xFF, 0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00 };
		const BYTE APDU_Command5[]  =    { 0xff, 0x00, 0x00, 0x00, 0x27, 0xd4, 0x8c, 0x05, 0x04, 0x00, 0x12, 0x34, 0x56, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		cbBuffer = 32;	// ? byte reply expected
		
		while (true) {
			SendRecvReader(&hDual, APDU_Command5, sizeof(APDU_Command5), Buffer, &cbBuffer);	// execute command

			// make sure this operation was successful, terminate if not
			if (!(Buffer[0] == 0xd5 && Buffer[1] == 0x8d )) {		// && Buffer[2] == 0x08
				/*
				PrintHex(&Buffer[0], 1);
				PrintHex(&Buffer[1], 1);
				PrintHex(&Buffer[2], 1);
				PrintHex(&Buffer[3], 1);
				PrintHex(&Buffer[4], 1);
				PrintHex(&Buffer[5], 1);
				PrintHex(&Buffer[6], 1);
				PrintHex(&Buffer[7], 1);
				PrintHex(&Buffer[8], 1);
				PrintHex(&Buffer[9], 1);
				PrintHex(&Buffer[10], 1);

				CloseReader(&hDual);
				wprintf(L"Error code received. Aborting..\n");
				return 1;
				*/
				wprintf(L"Failed..\n");
			}
			else {
				wprintf(L"Success! Reader is now in emulation mode.\n");
				break;
			}
		}

		// done, close reader
		CloseReader(&hDual);
	}
	else {
		wprintf(L"Failed to find NFC reader or tag (tag must be present).\n");
	}

	return 0;
}

int main() {
	EmulateNTag();
	return 0;
}
