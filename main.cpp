#include "stdafx.h"
#include "xml\\xml3all.h"

#include "compressor.hpp"
#include ".\eq\\alldspfilters_c.cpp"

#pragma comment(lib,"Mfplat.lib")
#pragma comment(lib,"mfreadwrite.lib")
#pragma comment(lib,"mfuuid.lib")
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
HRESULT ReadFileX(const wchar_t* f,std::function<HRESULT(int,float*,int,int,unsigned long long, unsigned long long)> foo)
{
	CComPtr<IMFSourceReader> pReader;
	auto hr = MFCreateSourceReaderFromURL(f, 0, &pReader);
	if (FAILED(hr)) return hr;
	hr = pReader->SetStreamSelection(
		(DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	if (FAILED(hr)) return hr;
	hr = pReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
	if (FAILED(hr)) return hr;

	// Create a partial media type that specifies uncompressed PCM audio.
	CComPtr<IMFMediaType> pPartialType;
	hr = MFCreateMediaType(&pPartialType);
	if (FAILED(hr)) return hr;
	hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (FAILED(hr)) return hr;
	hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
	if (FAILED(hr)) return hr;

/*	int SR = 48000;
	hr = pPartialType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, SR);
	if (FAILED(hr)) return hr;
*/
	hr = pReader->SetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
		NULL, pPartialType);
	if (FAILED(hr)) return hr;

	// Get the complete uncompressed format.
	CComPtr<IMFMediaType> pUncompressedAudioType;
	hr = pReader->GetCurrentMediaType(
		(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
		&pUncompressedAudioType);
	if (FAILED(hr)) return hr;

	// Ensure the stream is selected.
	hr = pReader->SetStreamSelection(
		(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
		TRUE);
	if (FAILED(hr)) return hr;

	// Number of channels
	UINT32 nCh = 0;
	hr = pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &nCh);
	if (FAILED(hr)) return hr;

	UINT32 aSR = 0;
	hr = pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &aSR);
	if (FAILED(hr)) return hr;

	// determine size
	PROPVARIANT var;
	PropVariantInit(&var);
	hr = pReader->GetPresentationAttribute(
		(DWORD)MF_SOURCE_READER_MEDIASOURCE,
		MF_PD_DURATION,
		&var
	);
	unsigned long long Duration = 0;
	if (SUCCEEDED(hr))
		Duration = var.hVal.QuadPart;

	foo(aSR, 0, 0, nCh,0,Duration);
	while (true)
	{
		DWORD dwFlags = 0;
		CComPtr<IMFSample> pSample;

		LONGLONG ts = 0;
		// Read the next sample.
		hr = pReader->ReadSample(
			(DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0, NULL, &dwFlags, &ts, &pSample);

		if (FAILED(hr)) break;
		if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
			break;

		if (pSample == NULL)
			continue;

		// Get a pointer to the audio data in the sample.
		CComPtr<IMFMediaBuffer> pBuffer;
		hr = pSample->ConvertToContiguousBuffer(&pBuffer);

		if (FAILED(hr)) { break; }

		BYTE* pAudioData = 0;
		DWORD cbBuffer = 0;
		hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);

		if (FAILED(hr)) { break; }

		hr = foo(aSR,(float*)pAudioData,cbBuffer/sizeof(float)/nCh,nCh,ts,Duration);
		pBuffer->Unlock();
		pAudioData = NULL;

		// This is an interleaved buffer
//		std::vector<std::vector<float>> rr;
//		InterleavedToSplit<float>((float*)pAudioData, cbBuffer / sizeof(float), nCh, rr);
//		for (UINT32 i = 0; i < nCh; i++)
//			AE::Write(hY[i], (char*)rr[i].data(), rr[i].size() * sizeof(float));

		// Unlock the buffer.

		if (FAILED(hr)) { break; }
	}
	return S_OK;

}


bool Ending = true;
using namespace std;
#include "mt\\rw.hpp"
#include "eq\\wave.h"
std::shared_ptr<WOUT> wout;
std::recursive_mutex mu;

COMP prx;

// Voice Capture
std::vector<std::vector<float>> CurrentVC;
std::recursive_mutex CurrentVCm;
int VC(int SR)
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


CONV conv;
void StartMP3(std::wstring fi)
{
	int sr = 0;
	int nch = 0;
	Ending = false;

	std::vector<char> dx;

	auto foo = [&](int SR, float* d, int nfr, int nCh, unsigned long long at, unsigned long long dur) -> HRESULT
	{
		if (sr == 0 || nch == 0)
		{
			sr = SR;
			nch = nCh;
			wout = 0;
			wout = make_shared<WOUT>();
			wout->Open(WAVE_MAPPER, SR, 16, nCh);
			std::lock_guard<std::recursive_mutex> lg(mu);
			prx.Build(SR, 0, nCh);
			std::thread t2(VC,SR);
			t2.detach();

		}
		if (d && nfr)
		{
			int BS = 1000;
			for (int ifr = 0; ifr < nfr; ifr += BS)
			{
				int lfr = nfr - ifr;
				if (lfr > BS)
					lfr = BS;

				std::array<float*,100> bin;
				std::array<float*,100> bout;
				std::vector<std::vector<float>> xin(nCh);
				std::vector<std::vector<float>> xout(nCh);
				for (int i = 0; i < nCh; i++)
				{
					xin[i].resize(lfr);
					bin[i] = xin[i].data();
					xout[i].resize(lfr);
					bout[i] = xout[i].data();
				}
				int n = 0;
				float* d2x = d + ifr * nCh;
				for (int i = 0; i < lfr; i++)
				{
					for (int ich = 0; ich < nCh; ich++)
					{
						xin[ich][i] = d2x[n++];
					}
				}

				std::lock_guard<std::recursive_mutex> lg(mu);

				if (prx.GetChains().size() == 1 && prx.GetChains()[0].A)
				{
					std::lock_guard<std::recursive_mutex> lg(CurrentVCm);
					auto s = CurrentVC[0].size();
					if (s >= lfr)
					{
						prx.process(SR, nCh, bin.data(), lfr, bout.data(), &CurrentVC, 0);
						CurrentVC[0].erase(CurrentVC[0].begin(), CurrentVC[0].begin() + lfr);
					}
					else
					{
						prx.process(SR, nCh, bin.data(), lfr, bout.data(), 0, 0);
					}
				}
				else
					prx.process(SR, nCh, bin.data(), lfr, bout.data(), 0, 0);

				n = 0;
				vector<float> d2(lfr * nCh);
				for (int i = 0; i < lfr; i++)
				{
					for (int ich = 0; ich < nCh; ich++)
					{
						d2[n++] = xout[ich][i];
					}
				}

				vector<short> d4(lfr * nCh * 2);
				conv.f32t16(d2.data(), lfr * nCh, d4.data());
				if (wout)
					wout->Write((const char*)d4.data(), lfr * nCh * 2);
			}
		}

		if (Ending)
			return E_FAIL;
		return S_OK;
	};
	ReadFileX(fi.c_str(), foo);
	wout = 0;
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
						auto fi = OpenSingleFile(0, L"*.*\0\0", 0, 0, 0, 0);
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
	MFStartup(MF_VERSION,0);

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

