#include "stdafx.h"
#include "xml\\xml3all.h"

#include "compressor.hpp"
#include ".\eq\\alldspfilters_c.cpp"



unsigned long GetNextMP3Frame(unsigned char* dat, unsigned int ds, int& st)
{
	// Find a 0xFF with one with 3 
	for (unsigned int i = st; i < (ds - 1); i++)
	{
		unsigned char d0 = dat[i];
		unsigned char d1 = dat[i + 1];
		//		unsigned char d2 = dat[i + 2];
		//		unsigned char d3 = dat[i + 2];
		if (d0 != 0xFF)
			continue;
		if ((d1 & 0xE0) != 0xE0)
			continue;
		int Ly = ((d1 >> 1) & 3);

		if (Ly != 1)
			continue;

		st = i;

		unsigned int j = 0;
		memcpy(&j, dat + i, 4);
		return j;
	}
	return 0;
}


void ReadMP3Header(FILE* fp, int& sr, int& nCh)
{
	int CP = ftell(fp);
	std::vector<unsigned char> dat(100000);
	size_t ds = fread(dat.data(), 1, 100000, fp);
	fseek(fp, CP, SEEK_SET);

	if (ds == 0)
		return;


	int st = 0;

	for (int jF = 0; jF < 1; jF++)
	{
		size_t hdr = GetNextMP3Frame(dat.data(), (unsigned int)ds, st);
		if (hdr == 0)
			break; // duh

		// This is it
//		DWORD a = dat[st];
//		DWORD b = dat[st + 1];
		DWORD c = dat[st + 2];
		DWORD d = dat[st + 3];

		st++; // To go to next


		unsigned int SRT = (c & 0xC) >> 2;
		switch (SRT)
		{
			case 0:
				sr = 44100;
				break;
			case 1:
				sr = 48000;
				break;
			case 2:
				sr = 32000;
				break;
		}

		unsigned int CHT = (d & 0xC0) >> 6;
		switch (CHT)
		{
			case 3:
				nCh = 1;
				break;
			default:
				nCh = 2;
				break;
		}
	}

}


bool Ending = true;
using namespace std;
#include "mt\\rw.hpp"
#include "eq\\wave.h"
#define MP3_BLOCK_SIZE 522
CONV conv;
std::shared_ptr<WOUT> wout;
int SR = 48000;
int BS = 100;
int BR = 32;
std::recursive_mutex mu;

COMP prx;

// Voice Capture
std::vector<std::vector<float>> CurrentVC;
std::recursive_mutex CurrentVCm;
int VC()
{
#define REFTIMES_PER_SEC  5000000
#define REFTIMES_PER_MILLISEC  5000
	CoInitialize(0);
	HRESULT hr = 0;
	CComPtr<IMMDeviceEnumerator> pEnumerator;
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
		(void**)&pEnumerator);
	if (FAILED(hr))
		return 0;

	CComPtr<IMMDevice> pDevice;
	hr = pEnumerator->GetDefaultAudioEndpoint(
		eCapture, eConsole, &pDevice);
	if (FAILED(hr))
		return 0;

	CComPtr<IAudioClient> pAudioClient;
	hr = pDevice->Activate(
		__uuidof(IAudioClient), CLSCTX_ALL,
		NULL, (void**)&pAudioClient);
	if (FAILED(hr))
		return 0;


	WAVEFORMATEX* pwfx = NULL;
	hr = pAudioClient->GetMixFormat(&pwfx);
	if (FAILED(hr))
		return 0;

	WAVEFORMATEXTENSIBLE w2 = {};
	w2.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	w2.Format.nChannels = 1;
	w2.Format.wBitsPerSample = 32;
	w2.Format.nSamplesPerSec = SR;
	w2.Format.nBlockAlign = 4;
	w2.Format.nAvgBytesPerSec = SR * 4;
	w2.Format.cbSize = 22;// sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	w2.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	w2.Samples.wSamplesPerBlock = 0;
	w2.Samples.wValidBitsPerSample = 32;

	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	hr = pAudioClient->Initialize(
		AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
		hnsRequestedDuration,
		0,
		&w2.Format,
		NULL);
	if (FAILED(hr))
		return 0;

	UINT32 bufferFrameCount = 0;
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	if (FAILED(hr))
		return 0;

	CComPtr<IAudioCaptureClient> pCaptureClient = NULL;
	hr = pAudioClient->GetService(
		__uuidof(IAudioCaptureClient),
		(void**)&pCaptureClient);
	if (FAILED(hr))
		return 0;

	hnsActualDuration = (REFERENCE_TIME)(REFTIMES_PER_SEC *
		(double)bufferFrameCount / (double)pwfx->nSamplesPerSec);

	hr = pAudioClient->Start();  // Start recording.
	if (FAILED(hr))
		return 0;

	std::vector<float> x;
	
	while (!Ending)
	{
		// Sleep for half the buffer duration.
		Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

		UINT32 packetLength = 0;
		hr = pCaptureClient->GetNextPacketSize(&packetLength);
		if (FAILED(hr))
			break;

		while (packetLength != 0)
		{
			BYTE* pData = 0;
			DWORD flags = 0;

			// Get the available data in the shared buffer.
			UINT32 numFramesAvailable;
			hr = pCaptureClient->GetBuffer(
				&pData,
				&numFramesAvailable,
				&flags, NULL, NULL);
			if (FAILED(hr))
				break;

			// Use the data
			// Copy the available capture data to the audio sink.
			// pData, numFramesAvailable
			auto fl = (float*)pData;
			if (prx.GetChains().size() > 0 && prx.GetChains()[0].A)
			{
				std::lock_guard<std::recursive_mutex> lg(CurrentVCm);
				auto s = CurrentVC[0].size();
				CurrentVC[0].resize(numFramesAvailable + s);
				memcpy(CurrentVC[0].data() + s, fl, numFramesAvailable * sizeof(float));
			}

			hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
			if (FAILED(hr))
				break;

			hr = pCaptureClient->GetNextPacketSize(&packetLength);
			if (FAILED(hr))
				break;
		}

	}


	hr = pAudioClient->Stop();  // Stop recording.

	return 1;
}


int nCh = 1;
void StartMP3(std::wstring fi)
{
	using namespace std;
	FILE* fp = 0;
	_wfopen_s(&fp, fi.c_str(), L"rb");
	if (!fp)
		return;
	ReadMP3Header(fp, SR, nCh);
	if (!SR)
		return;
	BS = SR / 10;
	BR = 32;

	wout = 0;
	wout = make_shared<WOUT>();
	wout->Open(WAVE_MAPPER, SR, 16,nCh);

	vector<char> ww(1000);
	WAVEFORMATEX* w2 = (WAVEFORMATEX*)ww.data();
	WAVEFORMATEX& w = *w2;
	w.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
	//		SR = 44100;


	w.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
	w.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
	w.nChannels = (WORD)nCh;
	w.nAvgBytesPerSec = 128 * (1024 / 8);  // not really used but must be one of 64, 96, 112, 128, 160kbps
	w.wBitsPerSample = 0;                  // MUST BE ZERO
	w.nBlockAlign = 1;                     // MUST BE ONE
	w.nSamplesPerSec = SR;              // 44.1kHz
	MPEGLAYER3WAVEFORMAT* mp3format = (MPEGLAYER3WAVEFORMAT*)&w;
	mp3format->fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
	mp3format->nBlockSize = MP3_BLOCK_SIZE;             // voodoo value #1
	mp3format->nFramesPerBlock = 1;                     // MUST BE ONE
	mp3format->nCodecDelay = 1393;                      // voodoo value #2
	mp3format->wID = MPEGLAYER3_ID_MPEG;

	// This is a raw stream, based on ACM, so start decompression
	vector<char> e(1000);
	WAVEFORMATEX* wDest = (WAVEFORMATEX*)e.data();
	wDest->wFormatTag = WAVE_FORMAT_PCM;
	wDest->nSamplesPerSec = w.nSamplesPerSec;
	wDest->nChannels = (WORD)nCh;
	wDest->wBitsPerSample = 16;
	auto f = acmFormatSuggest(0, &w, wDest, 1000, ACM_FORMATSUGGESTF_NSAMPLESPERSEC | ACM_FORMATSUGGESTF_WFORMATTAG | ACM_FORMATSUGGESTF_NCHANNELS | ACM_FORMATSUGGESTF_WBITSPERSAMPLE);
	if (f)
		return;
	SR = wDest->nSamplesPerSec;
	BS = SR / 10;

	HACMSTREAM has = 0;
	f = acmStreamOpen(&has, 0, &w, wDest, 0, 0, 0, 0);
	if (f)
		return;

	DWORD InputSize = 3145728;
	InputSize = MP3_BLOCK_SIZE;//*2000;
	InputSize *= 100;

	DWORD OutputSize = 0;
	acmStreamSize(has, InputSize, &OutputSize, ACM_STREAMSIZEF_SOURCE);

	vector<char> in(InputSize + 1);
	vector<char> out(OutputSize + 1);
	ACMSTREAMHEADER ash = { 0 };
	ash.cbStruct = sizeof(ash);
	ash.pbSrc = (LPBYTE)in.data();
	ash.cbSrcLength = InputSize;
	ash.pbDst = (LPBYTE)out.data();
	ash.cbDstLength = OutputSize;
	f = acmStreamPrepareHeader(has, &ash, 0);

	vector<char> dx;
	size_t BytesRead = 0;
	Ending = false;
	if (true)
	{
		std::lock_guard<std::recursive_mutex> lg(mu);
		prx.Build(SR,0,nCh);

	}

	std::thread t2(VC);
	t2.detach();


	for (;;)
	{
		size_t FBR = fread(in.data(), 1, InputSize, fp);
		if (FBR == 0)
			break;
		BytesRead += FBR;
		ash.cbSrcLength = (DWORD)FBR;
		f = acmStreamConvert(has, &ash, 0);

		if (ash.cbSrcLengthUsed != ash.cbSrcLength)
			fseek(fp, (signed)(ash.cbSrcLengthUsed - ash.cbSrcLength), SEEK_CUR);

		auto ds = dx.size();
		dx.resize(ds + ash.cbDstLengthUsed);
		memcpy(dx.data() + ds, out.data(), ash.cbDstLengthUsed);


		for (;;)
		{
			if (dx.size() < (size_t)(BS * 2))
				break;

			if (Ending)
				break;
			// Convert
			int fsz = BS;
			vector<float> d2(fsz);
			conv.f16t32((const short*)dx.data(), fsz, d2.data());

			if (nCh == 1)
			{
				vector<float> de = d2;
				float* fde = de.data();
				float* fd2 = d2.data();
				std::lock_guard<std::recursive_mutex> lg(mu);
				if (prx.GetChains().size() == 1 && prx.GetChains()[0].A)
				{
					std::lock_guard<std::recursive_mutex> lg(CurrentVCm);
					auto s = CurrentVC[0].size();
					if (s >= fsz)
					{
						prx.process(SR, nCh,&fd2, fsz, &fde,&CurrentVC, 0);
						CurrentVC[0].erase(CurrentVC[0].begin(), CurrentVC[0].begin() + fsz);
					}
					else
					{
						prx.process(SR, nCh,&fd2, fsz, &fde, 0, 0);
					}
				}
				else
					prx.process(SR, nCh, &fd2, fsz, &fde, 0, 0);
				vector<short> d4(fsz * 2);
				memcpy(d2.data(), de.data(), fsz * sizeof(float));
				conv.f32t16(d2.data(), fsz, d4.data());
				if (wout)
					wout->Write((const char*)d4.data(), fsz * 2);

			}
			if (nCh == 2)
			{
				// d2 is interleaved
				int bT = fsz / nCh;
				std::vector<float*> bin(nCh);
				std::vector<float*> bout(nCh);
				std::vector<std::vector<float>> xin(nCh);
				std::vector<std::vector<float>> xout(nCh);
				for (int i = 0; i < nCh; i++)
				{
					xin[i].resize(bT);
					bin[i] = xin[i].data();
					xout[i].resize(bT);
					bout[i] = xout[i].data();
				}
				int n = 0;
				for (int i = 0; i < bT; i++)
				{
					for (int ich = 0; ich < nCh; ich++)
					{
						xin[ich][i] = d2[n++];
					}
				}
				prx.process(SR, nCh, bin.data(), bT, bout.data(),0,0); //*
				n = 0;
				for (int i = 0; i < bT; i++)
				{
					for (int ich = 0; ich < nCh; ich++)
					{
						d2[n++] = xout[ich][i];
					}
				}

				vector<short> d4(fsz);
				conv.f32t16(d2.data(), fsz, d4.data());
				if (wout)
					wout->Write((const char*)d4.data(), fsz*2);
			}


			dx.erase(dx.begin(), dx.begin() + BS * 2);
		}

	}

	acmStreamUnprepareHeader(has, &ash, 0);
	acmStreamClose(has, 0);
	fclose(fp);

}

std::wstring OpenSingleFile(HWND hh, const wchar_t* filter, int fidx, const wchar_t* initf, const wchar_t* dir, const wchar_t* title)
{
	OPENFILENAME of = { 0 };
	of.lStructSize = sizeof(of);
	of.hwndOwner = hh;
	of.lpstrFilter = filter;
	of.lpstrInitialDir = dir;
	of.nFilterIndex = fidx;
	std::vector<wchar_t> fnx(10000);
	of.lpstrFile = fnx.data();
	if (initf)
		wcscpy_s(fnx.data(), 10000, initf);
	of.nMaxFile = 10000;
	of.lpstrTitle = title;
	of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if (!GetOpenFileName(&of))
		return std::wstring(L"");
	return std::wstring(fnx.data());
}

HWND MainWindow = 0;



class M : public COMPCALLBACK
{
public:

	M()
	{
	}

	virtual void RedrawRequest(COMP*)
	{
		InvalidateRect(MainWindow, 0, TRUE);
		UpdateWindow(MainWindow);
	}
	virtual void Dirty(COMP* e, bool)
	{

	}

};

std::shared_ptr<M> cxt;

CComPtr<ID2D1HwndRenderTarget> d;
CComPtr<ID2D1Factory> fa;


LRESULT CALLBACK Main_DP(HWND hh, UINT mm, WPARAM ww, LPARAM ll)
{
	switch (mm)
	{
		case WM_CREATE:
		{
			MainWindow = hh;
			prx.SetWindow(hh);
			cxt = std::make_shared<M>();
			
			prx.AddCallback(cxt);
			XML3::XML x("comp.xml");
			prx.Unser(x.GetRootElement());
			prx.GetChains().clear();
			COMPCHAIN cc = { CLSID_AboutProtocol, L"Voice Capture",-15.0f, 0 };
			prx.GetChains().emplace_back(cc);


			SetTimer(hh, 1, 100, 0);
			x.Save();

#ifndef _DEBUG
			MessageBox(hh, L"Press space to select MP3 to play", L"", 0);
			#endif
			break;
		}

		case WM_TIMER:
		{
			InvalidateRect(hh, 0, true);
			UpdateWindow(hh);
			return 0;
		}

		case WM_COMMAND:
		{
			int LW = LOWORD(ww);
			UNREFERENCED_PARAMETER(LW);

			if (LW == 101)
			{

			}
			return 0;
		}

		case WM_CLOSE:
		{
			XML3::XML x("comp.xml");
			prx.Ser(x.GetRootElement());
			x.Save();
			DestroyWindow(hh);
			return 0;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			prx.KeyDown(ww, ll);
			if (ww == VK_SPACE)
			{
				if (!Ending)
				{
					Ending = true;
				}
				else
				{
					auto thr = []()
					{
						auto fi = OpenSingleFile(0, L"*.mp3\0\0", 0, 0, 0, 0);
						StartMP3(fi.c_str());
					};
					std::thread t(thr);
					t.detach();

				}
			}
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			prx.MouseMove(ww, ll);
			return 0;
		}
		case WM_MOUSEWHEEL:
		{
			prx.MouseWheel(ww, ll);
			return 0;
		}
		case WM_LBUTTONDOWN:
		{
			prx.LeftDown(ww, ll);
			return 0;
		}
		case WM_RBUTTONDOWN:
		{
			prx.RightDown(ww, ll);
			return 0;
		}
		case WM_LBUTTONUP:
		{
			prx.LeftUp(ww, ll);
			return 0;
		}
		case WM_LBUTTONDBLCLK:
		{
			prx.LeftDoubleClick(ww, ll);
			return 0;
		}

		case WM_ERASEBKGND:
		{
			return 1;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps = {};
			BeginPaint(hh, &ps);

			RECT rc;
			GetClientRect(hh, &rc);
			if (!fa)
				D2D1CreateFactory(D2D1_FACTORY_TYPE::D2D1_FACTORY_TYPE_MULTI_THREADED, &fa);
			if (!d)
			{
				D2D1_HWND_RENDER_TARGET_PROPERTIES hp;
				hp.hwnd = hh;
				hp.pixelSize.width = rc.right;
				hp.pixelSize.height = rc.bottom;
				d.Release();

				fa->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hh, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &d);
			}
			d->BeginDraw();
			prx.PaintWindow = hh;
			prx.Paint(fa, d, rc);
			d->EndDraw();
			EndPaint(hh, &ps);
			return 0;
		}

		case WM_SIZE:
		{
			if (!d)
				return 0;

			RECT rc;
			GetClientRect(hh, &rc);
			D2D1_SIZE_U u;
			u.width = rc.right;
			u.height = rc.bottom;
			d->Resize(u);
			return 0;
		}

		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}
	}
	return DefWindowProc(hh, mm, ww, ll);
}


int __stdcall WinMain(HINSTANCE h, HINSTANCE, LPSTR, int)
	{
	CurrentVC.resize(1);

	WSADATA wData;
	WSAStartup(MAKEWORD(2, 2), &wData);
	CoInitializeEx(0, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icex = { 0 };
	UpdateThread();

	COMPBAND b[3];
	b[0].from = 0;
	b[0].to = 400.0f / 22000.0f;
	b[0].comp.threshold = -12;
	b[0].comp.ratio = 2;
	b[1].from = b[0].to;
	b[1].to = 2000.0f / 22000.0f;
	b[1].comp.threshold = -24;
	b[1].comp.ratio = 4;
	b[2].from = b[1].to;
	b[2].to = 1;
	b[2].comp.threshold = -6;
	b[2].comp.ratio = 10;
	COMP c({ b[0],b[1],b[2] });
	c.fmodelog = 1;

	

	prx = c;

	WNDCLASSEX wClass = { 0 };

	wClass.cbSize = sizeof(wClass);

	wClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW | CS_PARENTDC;
	wClass.lpfnWndProc = (WNDPROC)Main_DP;
	wClass.hInstance = h;
	wClass.hIcon = 0;
	wClass.hCursor = LoadCursor(0, IDC_ARROW);
	wClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wClass.lpszClassName = _T("CLASS");
	wClass.hIconSm = 0;
	RegisterClassEx(&wClass);


	MainWindow = CreateWindowEx(0,
		_T("CLASS"),
		L"Compressor+Expander+Gate",
		WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS |
		WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, h, 0);

	ShowWindow(MainWindow, SW_SHOW);


	MSG msg;

	HACCEL acc = LoadAccelerators(h, L"MENU_1");
	while (GetMessage(&msg, 0, 0, 0))
	{
		if (TranslateAccelerator(msg.hwnd, acc, &msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	ExitProcess(0);
	return 0;
	}

