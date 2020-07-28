#ifdef TURBO_PLAY
#include "..\\\AED\\\fft.hpp"
#else
#include ".\\eq\alldspfilters_h.hpp"
#include ".\\eq\fft.hpp"
void nop() {}
#define shared_ptr_debug shared_ptr
#define make_shared_debug make_shared
#endif


typedef Dsp::SimpleFilter<Dsp::Butterworth::LowPass<100>, 10> ButtLP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevI::LowPass<100>, 10> Che1LP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevII::LowPass<100>, 10> Che2LP;
typedef Dsp::SimpleFilter<Dsp::Elliptic::LowPass<100>, 10> EllLP;

typedef Dsp::SimpleFilter<Dsp::Butterworth::HighPass<100>, 10> ButtHP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevI::HighPass<100>, 10> Che1HP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevII::HighPass<100>, 10> Che2HP;
typedef Dsp::SimpleFilter<Dsp::Elliptic::HighPass<100>, 10> EllHP;

typedef Dsp::SimpleFilter<Dsp::Butterworth::BandPass<100>, 10> ButtBP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevI::BandPass<100>, 10> Che1BP;
typedef Dsp::SimpleFilter<Dsp::ChebyshevII::BandPass<100>, 10> Che2BP;
typedef Dsp::SimpleFilter<Dsp::Elliptic::BandPass<100>, 10> EllBP;

enum class COMPSTATE
{
	NONE = 0,
	ATTACK = 1,
	ACTION = 2,
	HOLD = 3,
	RELEASE = 4
};
enum class COMPMODE
{
	COMPRESSOR = 0,
	DEXPANDER = 1,
	UEXPANDER = 2,
	UDEXPANDER = 3,
	GATE = 4,
};


struct COMPPARAMS
{
	float threshold = -12.0f;
	float hysteresis = -15.0f;
	float ratio = 2.0f;
	float attack = 0.25f; // seconds
	float release = 0.15f; // seconds
	float hold = 0; // seconds
	float makeup = 0.0f; // dB
	int  AutoDeclipper = 0;

	COMPSTATE state = COMPSTATE::NONE;
	COMPMODE Mode = COMPMODE::COMPRESSOR;
	int SamplesLeftforProgress = 0; // Starts at max AttackSamples or ReleaseSamples, goes minus

	bool IsExpander()
	{
		if (Mode == COMPMODE::UDEXPANDER || Mode == COMPMODE::UEXPANDER || Mode == COMPMODE::DEXPANDER)
			return true;
		return false;
	}


	virtual void Ser(XML3::XMLElement& e)
	{
		e.vv("mode").SetValueInt((int)Mode);
		e.vv("threshold").SetValueFloat(threshold);
		e.vv("hysteresis").SetValueFloat(hysteresis);
		e.vv("ratio").SetValueFloat(ratio);
		e.vv("attack").SetValueFloat(attack);
		e.vv("hold").SetValueFloat(hold);
		e.vv("release").SetValueFloat(release);
		e.vv("makeup").SetValueFloat(makeup);
		e.vv("adc").SetValueInt(AutoDeclipper);
	}
	virtual void Unser(XML3::XMLElement& e)
	{
		Mode = (COMPMODE)e.vv("mode").GetValueInt(0);
		threshold = e.vv("threshold").GetValueFloat(-6);
		hysteresis = e.vv("hysteresis").GetValueFloat(-9);
		ratio = e.vv("ratio").GetValueFloat(2);
		attack = e.vv("attack").GetValueFloat(0.25f);
		hold = e.vv("hold").GetValueFloat(0.25f);
		release = e.vv("release").GetValueFloat(0.15f);
		makeup = e.vv("makeup").GetValueFloat(0);
		AutoDeclipper = e.vv("adc").GetValueInt(0);
	}


};

#define DYNAMIC_CHANNEL

struct COMPBAND
{
	float from = 0;
	float to = 1.0f; // 22Khz

	int Type = 0; // 0 Butterworth, 1 Chebyshev I, 2 Chebyshev II, 3 Elliptic
	int Order = 3;
	float ripple = 0.5f;
	float rolloff = 12.0f;

	virtual void Ser(XML3::XMLElement& e)
	{
		e.vv("from").SetValueFloat(from);
		e.vv("to").SetValueFloat(to);
		e.vv("type").SetValueInt(Type);
		e.vv("order").SetValueInt(Order);
		e.vv("ripple").SetValueFloat(ripple);
		e.vv("rolloff").SetValueFloat(rolloff);
		comp.Ser(e["comp"]);
	}
	virtual void Unser(XML3::XMLElement& e)
	{
		from = e.vv("from").GetValueFloat(0);
		to = e.vv("to").GetValueFloat(1);
		Type = e.vv("type").GetValueInt(0);
		Order = e.vv("order").GetValueInt(3);
		ripple = e.vv("ripple").GetValueFloat(0.5f);
		rolloff = e.vv("rolloff").GetValueFloat(12.0f);
		comp.Unser(e["comp"]);
	}

	float LastInputDB = -72;
	float LastInputDBC = -72;
	D2D1_RECT_F BandRect = {};

	D2D1_RECT_F ThresholdRect = {};
	D2D1_RECT_F HysteresisRect = {};
	D2D1_RECT_F RatioPoints1 = {};
	D2D1_RECT_F RatioPoints2 = {};

	D2D1_RECT_F ButtThres = {};
	D2D1_RECT_F ButtRatioHyst = {};
	D2D1_RECT_F ButtAttack = {};
	D2D1_RECT_F ButtHold = {};
	D2D1_RECT_F ButtRelease = {};
	D2D1_RECT_F ButtMakeup = {};

	std::shared_ptr_debug<void> sf;
#ifdef DYNAMIC_CHANNEL
	std::vector<std::vector<float>> din;
	std::vector<std::vector<float>> dout;
#else
	std::array<std::vector<float>, 2> in;
	std::array<std::vector<float>, 2> out;
#endif
	COMPPARAMS comp;
};

class COMP;

class COMPCALLBACK
{
public:

	virtual void RedrawRequest(COMP* pr) = 0;
	virtual void Dirty(COMP* e, bool) = 0;

#ifdef ENABLE_SHARED_PTR_DEBUG
	virtual ~COMPCALLBACK()
	{
	}
#endif

};

class MMCB : public COMPCALLBACK
{
public:
	HWND hC = 0;
	int SR;

	virtual void RedrawRequest(COMP* p);
	virtual void Dirty(COMP* q, bool);

#ifdef ENABLE_SHARED_PTR_DEBUG
	virtual ~MMCB()
	{
	}
#endif

};

struct COMPCHAIN
{
	CLSID g = {};
	std::wstring n;
	float thr = -18;
	bool A = false;
	D2D1_RECT_F thres_rect = { };
};

class COMP
{
	float MaxHz = 22000.0;
	HWND hParent = 0;
	std::vector<COMPBAND> b;
	float globalmakeup = 0.0f;
	float LastInputDBChain = -72;
	std::vector<COMPCHAIN> Chains;

	std::vector<std::vector<float>> dins;
	std::vector<std::vector<float>> douts;
	int ShowDataMode = 0;

	std::vector<std::shared_ptr_debug<COMPCALLBACK>> cbs;
	std::recursive_mutex mu;
	D2D1_RECT_F rc = {};
	CComPtr<IDWriteFactory> WriteFactory;
	CComPtr<IDWriteTextFormat> Text;
	CComPtr<ID2D1SolidColorBrush> BGBrush;
	CComPtr<ID2D1SolidColorBrush> WhiteBrush;
	CComPtr<ID2D1SolidColorBrush> GrayBrush;
	CComPtr<ID2D1SolidColorBrush> YellowBrush;
	CComPtr<ID2D1SolidColorBrush> SelectBrush;
	CComPtr<ID2D1SolidColorBrush> BlackBrush;

	D2D1_COLOR_F bg = { 0.1f,0.1f,0.1f,1.0f };
	D2D1_COLOR_F whitecolor = { 1.0f,1.0f,1.0f,1.0f };
	D2D1_COLOR_F graycolor = { 0.5f,0.5f,0.5f,0.6f };
	D2D1_COLOR_F yellowcolor = { 0.9f,0.9f,0.1f,0.5f };
	D2D1_COLOR_F selectcolor = { 0.0f,9.0f,3.0f,1.0f };
	D2D1_COLOR_F blackcolor = { 0.0f,0.0f,0.0f,1.0f };

	D2D1_RECT_F ChainRect = {};

	float db2lin(float db) { // dB to linear
		return powf(10.0f, 0.05f * db);
	}

	float lin2db(float lin) { // linear to dB
		return 20.0f * log10f(lin);
	}

	struct ASKTEXT
	{
		const wchar_t* ti;
		const wchar_t* as;
		wchar_t* re;
		int P;
		std::wstring* re2;
		int mx = 1000;
	};

	static INT_PTR CALLBACK A_DP(HWND hh, UINT mm, WPARAM ww, LPARAM ll)
	{
		static ASKTEXT* as = 0;
		switch (mm)
		{
			case WM_INITDIALOG:
			{
				as = (ASKTEXT*)ll;
				SetWindowText(hh, as->ti);
				if (as->P != 2)
				{
					SetWindowText(GetDlgItem(hh, 101), as->as);
					if (as->re)
						SetWindowText(GetDlgItem(hh, 102), as->re);
					if (as->re2)
						SetWindowText(GetDlgItem(hh, 102), as->re2->c_str());
				}
				else
					SetWindowText(GetDlgItem(hh, 701), as->as);
				if (as->P == 1)
				{
					auto w = GetWindowLongPtr(GetDlgItem(hh, 102), GWL_STYLE);
					w |= ES_PASSWORD;
					SetWindowLongPtr(GetDlgItem(hh, 102), GWL_STYLE, w);
				}
				return true;
			}

			case WM_COMMAND:
			{
				if (LOWORD(ww) == IDOK)
				{
					wchar_t re1[1000] = { 0 };
					wchar_t re2[1000] = { 0 };
//					MessageBox(0, L"foo", 0, 0);
					if (as->P == 2)
					{
						GetWindowText(GetDlgItem(hh, 101), re1, 1000);
						GetWindowText(GetDlgItem(hh, 102), re2, 1000);
						if (wcscmp(re1, re2) != 0 || wcslen(re1) == 0)
						{
							SetWindowText(GetDlgItem(hh, 101), L"");
							SetWindowText(GetDlgItem(hh, 102), L"");
							SetFocus(GetDlgItem(hh, 101));
							return 0;
						}
						wcscpy_s(as->re, 1000, re1);
						EndDialog(hh, IDOK);
						return 0;
					}

					if (as->re2)
					{
						int lex = GetWindowTextLength(GetDlgItem(hh, 102));
						std::vector<wchar_t> re(lex + 100);
						GetWindowText(GetDlgItem(hh, 102), re.data(), lex + 100);
						*as->re2 = re.data();
						EndDialog(hh, IDOK);
					}
					else
					{
						GetWindowText(GetDlgItem(hh, 102), as->re, as->mx);
						EndDialog(hh, IDOK);
					}
					return 0;
				}
				if (LOWORD(ww) == IDCANCEL)
				{
					EndDialog(hh, IDCANCEL);
					return 0;
				}
			}
		}
		return 0;
	}

	bool AskText(HWND hh, const TCHAR* ti, const TCHAR* as, TCHAR* re, std::wstring* re2 = 0, int mxt = 1000)
	{
		const char* res = "\xC4\x0A\xCA\x90\x00\x00\x00\x00\x04\x00\x00\x00\x00\x00\x6D\x01\x3E\x00\x00\x00\x00\x00\x00\x00\x0A\x00\x54\x00\x61\x00\x68\x00\x6F\x00\x6D\x00\x61\x00\x00\x00\x01\x00\x00\x50\x00\x00\x00\x00\x0A\x00\x09\x00\x1C\x01\x1A\x00\x65\x00\xFF\xFF\x82\x00\x00\x00\x00\x00\x00\x00\x80\x00\x81\x50\x00\x00\x00\x00\x0A\x00\x29\x00\x1D\x01\x0F\x00\x66\x00\xFF\xFF\x81\x00\x00\x00\x00\x00\x00\x00\x00\x03\x01\x50\x00\x00\x00\x00\x2F\x01\x16\x00\x32\x00\x0E\x00\x01\x00\xFF\xFF\x80\x00\x4F\x00\x4B\x00\x00\x00\x00\x00\x00\x00\x00\x03\x01\x50\x00\x00\x00\x00\x2F\x01\x29\x00\x32\x00\x0E\x00\x02\x00\xFF\xFF\x80\x00\x43\x00\x61\x00\x6E\x00\x63\x00\x65\x00\x6C\x00\x00\x00\x00\x00";
		ASKTEXT a = { ti,as,re,0,re2,mxt };
		return (DialogBoxIndirectParam(GetModuleHandle(0), (LPCDLGTEMPLATEW)res, hh, A_DP, (LPARAM)&a) == IDOK);
	}


	template <typename T = float> bool InRect(D2D1_RECT_F& r, T x, T y)
	{
		if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom)
			return true;
		return false;
	}


	D2D1_RECT_F FromR(RECT rcc)
	{
		D2D1_RECT_F r;
		r.left = (FLOAT)rcc.left;
		r.top = (FLOAT)rcc.top;
		r.right = (FLOAT)rcc.right;
		r.bottom = (FLOAT)rcc.bottom;
		return r;
	}

	bool Single()
	{
		if (b.size() != 1)
			return false;
		if (b[0].from != 0 || b[0].to != 1)
			return false;
		return true;
	}

	void DestroyBrushes()
	{
		BGBrush = 0;
		WhiteBrush = 0;
		GrayBrush = 0;
		YellowBrush = 0;
		BlackBrush = 0;
		SelectBrush = 0;
		Text = 0;
		WriteFactory = 0;
	}
	CComPtr<ID2D1SolidColorBrush> GetD2SolidBrush(ID2D1RenderTarget* p, D2D1_COLOR_F cc)
	{
		CComPtr<ID2D1SolidColorBrush> br = 0;
		p->CreateSolidColorBrush(cc, &br);
		return br;
	}
	void CreateBrushes(ID2D1RenderTarget* p, bool F = false)
	{
		if (WhiteBrush && !F)
			return; // OK

		SelectBrush = GetD2SolidBrush(p, selectcolor);
		WhiteBrush = GetD2SolidBrush(p, whitecolor);
		BlackBrush = GetD2SolidBrush(p, blackcolor);
		GrayBrush = GetD2SolidBrush(p, graycolor);
		YellowBrush = GetD2SolidBrush(p, yellowcolor);
		BGBrush = GetD2SolidBrush(p, bg);
		DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&WriteFactory);

		LOGFONT lf;
		GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
		DWRITE_FONT_STYLE fst = DWRITE_FONT_STYLE_NORMAL;
		if (lf.lfItalic)
			fst = DWRITE_FONT_STYLE_ITALIC;
		DWRITE_FONT_STRETCH fsr = DWRITE_FONT_STRETCH_NORMAL;
		FLOAT fs = (FLOAT)abs(lf.lfHeight);
		WriteFactory->CreateTextFormat(lf.lfFaceName, 0, lf.lfWeight > 500 ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, fst, fsr, fs, L"", &Text);
	}

	void Redraw()
	{
		for (auto cc : cbs)
			cc->RedrawRequest(this);
	}

	void Dirty(bool x)
	{
		for (auto cc : cbs)
			cc->Dirty(this, x);
	}



public:

	HWND PaintWindow = 0;
	std::vector<COMPCHAIN>& GetChains() { return  Chains; };

	std::vector<COMPBAND>& GetBands() { return b; }


	void SetWindow(HWND hh)
	{
		hParent = hh;
	}

	void RemoveCallbacks()
	{
		cbs.clear();
	}
	void AddCallback(std::shared_ptr_debug<COMPCALLBACK> cx)
	{
		cbs.push_back(cx);
	}

	void Ser(XML3::XMLElement& e)
	{
		e.RemoveAllElements();
		e.vv("lm").SetValueInt(fmodelog);
		e.vv("ds").SetValueInt(DataSize);
		e.vv("sdm").SetValueInt(ShowDataMode);
		if (globalmakeup > 0)
			e.vv("gmakeup").SetValueFloat(globalmakeup);
		wchar_t re[100] = {};
		e["chains"].RemoveAllElements();
		for (auto& ch : Chains)
		{
			auto& el = e["chains"].AddElement("ch");
			StringFromGUID2(ch.g, re, 100);
			el.vv("c").SetValue(re);
			el.vv("n").SetValue(ch.n);
			el.vv("thr").SetValueFloat(ch.thr);
			el.vv("a").SetValueInt(ch.A);
		}
		for (auto& bb : b)
		{
			bb.Ser(e["bands"].AddElement("band"));
		}
	}

	void Ensure()
	{
		if (b.empty())
		{
			COMPBAND cb;
			b.push_back(cb);
		}
	}

	void Unser(XML3::XMLElement& e)
	{
		b.clear();
		Chains.clear();
		globalmakeup = e.vv("gmakeup").GetValueFloat(0);
		fmodelog = e.vv("lm").GetValueInt(0);
		DataSize = e.vv("ds").GetValueInt(100);
		ShowDataMode = e.vv("sdm").GetValueInt();

		for (auto& els : e["bands"])
		{
			COMPBAND cb;
			cb.Unser(els);
			b.push_back(cb);
		}
		for (auto& els : e["chains"])
		{
			bool x = (bool)els.vv("a").GetValueInt();
			std::wstring c = els.vv("c").GetWideValue();
			std::wstring n = els.vv("n").GetWideValue();
			float thr = els.vv("thr").GetValueFloat(-12);
			CLSID cc;
			CLSIDFromString(c.c_str(), &cc);

			COMPCHAIN ch = { cc,n,thr,x };
			Chains.push_back(ch);
		}
		Ensure();
	}

	COMP(COMPMODE cm = COMPMODE::COMPRESSOR)
	{
		Ensure();
		for (auto& bb : b)
			bb.comp.Mode = cm;
	}


	void operator=(const COMP& comp)
	{
		fmodelog = comp.fmodelog;
		b = comp.b;
		globalmakeup = comp.globalmakeup;
		Chains = comp.Chains;
		Ensure();
	}
	COMP(const COMP& comp)
	{
		operator=(comp);
	}

	COMP(std::initializer_list<COMPBAND> l)
	{
		b = l;
	}


	void ResetAsMulti()
	{
		std::lock_guard<std::recursive_mutex> lg(mu);
		b.clear();
		b.resize(4);
		b[0].from = 0;
		b[0].to = 120.0f / MaxHz;
		b[1].from = b[0].to;
		b[1].to = 2000.0f / MaxHz;
		b[2].from = b[1].to;
		b[2].to = 10000.0f / MaxHz;
		b[3].from = b[2].to;
		b[3].to = 1.0f;
	}

	void reset()
	{
		std::lock_guard<std::recursive_mutex> lg(mu);
		for (auto& bb : b)
		{
			bb.sf = 0;
			bb.comp.state = COMPSTATE::NONE;
			bb.comp.SamplesLeftforProgress = 0;
		}
	}

	bool ChainHit(WPARAM ww, LPARAM ll)
	{
		float x = 0, y = 0;
		x = (FLOAT)LOWORD(ll);
		y = (FLOAT)HIWORD(ll);

		if (InRect<>(ChainRect, x, y) && !Chains.empty())
		{
			HMENU hPr = CreatePopupMenu();
			for (size_t i = 0; i < Chains.size(); i++)
			{
				AppendMenu(hPr, MF_STRING, i + 1,Chains[i].n.c_str());
				if (Chains[i].A)
				{
					CheckMenuItem(hPr, (UINT)(i + 1), MF_CHECKED);
				}
			}
			POINT po;
			GetCursorPos(&po);
			int tcmd = 0;
			if (Chains.size() == 1)
				tcmd = 1;
			else
				tcmd = TrackPopupMenu(hPr, TPM_CENTERALIGN | TPM_RETURNCMD, po.x, po.y, 0, hParent, 0);
			DestroyMenu(hPr);
			if (tcmd == 0)
				return true;

			tcmd--;
			Chains[tcmd].A = !Chains[tcmd].A;
			Redraw();

			return true;
		}


		return false;
	}

	virtual void RightDown(WPARAM ww, LPARAM ll)
	{
		wchar_t rr[1000] = {};
		float x = 0, y = 0;
		x = (FLOAT)LOWORD(ll);
		y = (FLOAT)HIWORD(ll);

		if (ChainHit(ww, ll))
			return;
		for (size_t ib = 0; ib < b.size(); ib++)
		{
			auto& bb = b[ib];
			if (InRect<>(bb.BandRect, x, y))
			{
				HMENU hPr = CreatePopupMenu();
				AppendMenu(hPr, MF_STRING, 71, L"Compressor");
				AppendMenu(hPr, MF_STRING, 72, L"Downwards Expander");
				AppendMenu(hPr, MF_STRING, 73, L"Upwards Expander");
				AppendMenu(hPr, MF_STRING, 74, L"Downwards + Upwards Expander");
				AppendMenu(hPr, MF_STRING, 75, L"Noise Gate");
				AppendMenu(hPr, MF_SEPARATOR, 0, L"");
				AppendMenu(hPr, MF_STRING, 90, L"Add band");
				if (b.size() > 1)
					AppendMenu(hPr, MF_STRING, 91, L"Delete this band");
				AppendMenu(hPr, MF_SEPARATOR, 0, L"");
				AppendMenu(hPr, MF_STRING, 50, L"Auto Prevent Clipping");
				if (bb.comp.AutoDeclipper)
					CheckMenuItem(hPr, bb.Type + 50, MF_CHECKED);
				AppendMenu(hPr, MF_SEPARATOR, 0, L"");

				if (b.size() > 1)
				{
					AppendMenu(hPr, MF_STRING, 51, L"Butterworth");
					AppendMenu(hPr, MF_STRING, 52, L"Chebyshev I");
					AppendMenu(hPr, MF_STRING, 53, L"Chebyshev II");
					AppendMenu(hPr, MF_STRING, 54, L"Elliptic");
					AppendMenu(hPr, MF_SEPARATOR, 0, L"");
					CheckMenuItem(hPr, bb.Type + 51, MF_CHECKED);
				}
				AppendMenu(hPr, MF_STRING, 81, L"Logarithmic frequency view");
				AppendMenu(hPr, MF_STRING, 82, L"Linear frequency view");
				if (b.size() > 1)
				{
					AppendMenu(hPr, MF_SEPARATOR, 0, L"");
					AppendMenu(hPr, MF_STRING, 83, L"Global makeup gain...");
				}
				AppendMenu(hPr, MF_SEPARATOR, 0, L"");
				AppendMenu(hPr, MF_STRING, 141, L"Live data off");
				AppendMenu(hPr, MF_STRING, 142, L"Live data signal");
				AppendMenu(hPr, MF_STRING, 143, L"Live data FFT");
				CheckMenuItem(hPr, ShowDataMode + 141, MF_CHECKED);

				AppendMenu(hPr, MF_SEPARATOR, 0, L"");
				AppendMenu(hPr, MF_STRING, 101, L"Save preset...");
				AppendMenu(hPr, MF_STRING, 102, L"Load preset...");

				if (fmodelog)
					CheckMenuItem(hPr, 81, MF_CHECKED);
				else
					CheckMenuItem(hPr, 82, MF_CHECKED);
				if (bb.comp.Mode == COMPMODE::GATE)
					CheckMenuItem(hPr, 75, MF_CHECKED);
				else
					if (bb.comp.Mode == COMPMODE::UDEXPANDER)
						CheckMenuItem(hPr, 74, MF_CHECKED);
					else
						if (bb.comp.Mode == COMPMODE::UEXPANDER)
							CheckMenuItem(hPr, 73, MF_CHECKED);
						else
							if (bb.comp.Mode == COMPMODE::DEXPANDER)
								CheckMenuItem(hPr, 72, MF_CHECKED);
							else
								CheckMenuItem(hPr, 71, MF_CHECKED);

				POINT po;
				GetCursorPos(&po);
				int tcmd = TrackPopupMenu(hPr, TPM_CENTERALIGN | TPM_RETURNCMD, po.x, po.y, 0, hParent, 0);
				DestroyMenu(hPr);
				if (tcmd == 0)
					return;

				if (tcmd == 141)
					ShowDataMode = 0;
				if (tcmd == 142)
					ShowDataMode = 1;
				if (tcmd == 143)
					ShowDataMode = 2;


				if (tcmd == 101)
				{
					// Save preset
					OPENFILENAME of = { 0 };
					of.lStructSize = sizeof(of);
					of.lpstrDefExt = L"xml";
					of.hwndOwner = hParent;
					of.lpstrFilter = L"*.xml\0\0";
					std::vector<wchar_t> fnx(10000);
					of.lpstrFile = fnx.data();
					of.nMaxFile = 10000;
					of.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
					if (GetSaveFileName(&of))
					{
						DeleteFile(fnx.data());
						XML3::XML x4(fnx.data());
						Ser(x4.GetRootElement());
						x4.Save();
					}
				}
				if (tcmd == 102)
				{
					OPENFILENAME of = { 0 };
					of.lStructSize = sizeof(of);
					of.hwndOwner = hParent;
					of.lpstrFilter = L"*.xml\0\0";
					std::vector<wchar_t> fnx(10000);
					of.lpstrFile = fnx.data();
					of.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
					of.nMaxFile = 10000;
					if (GetOpenFileName(&of))
					{
						XML3::XML x5(fnx.data());
						Unser(x5.GetRootElement());
					}
				}
				if (tcmd == 83)
				{
					swprintf_s(rr, 1000, L"%.1f", globalmakeup);
					if (!AskText(hParent, L"Global make up gain", L"Enter global make up gain (dB):", rr))
						return;
					globalmakeup = (float)_wtof(rr);
					if (globalmakeup <= 0)
						globalmakeup = 0;
					Redraw();

				}

				if (tcmd == 50)
				{
					bb.comp.AutoDeclipper = !bb.comp.AutoDeclipper;
					Redraw();
					return;
				}

				if (tcmd >= 51 && tcmd <= 54)
				{
					bb.Type = tcmd - 51;
					reset();
					Redraw();
					return;
				}

				if (tcmd == 71)
				{
					bb.comp.Mode = COMPMODE::COMPRESSOR;
					reset();
					Redraw();
					return;
				}
				if (tcmd == 72)
				{
					bb.comp.Mode = COMPMODE::DEXPANDER;
					reset();
					Redraw();
					return;
				}
				if (tcmd == 73)
				{
					bb.comp.Mode = COMPMODE::UEXPANDER;
					reset();
					Redraw();
					return;
				}
				if (tcmd == 74)
				{
					bb.comp.Mode = COMPMODE::UDEXPANDER;
					reset();
					Redraw();
					return;
				}
				if (tcmd == 75)
				{
					bb.comp.Mode = COMPMODE::GATE;
					reset();
					Redraw();
					return;
				}

				if (tcmd == 81)
				{
					fmodelog = 1;
					Redraw();
					return;
				}
				if (tcmd == 82)
				{
					fmodelog = 0;
					Redraw();
					return;
				}

				if (tcmd == 90)
				{
					float nhz = (float)X2Freqr(x);
					float X = nhz / (float)MaxHz;

					auto ot = bb.to;
					bb.to = X;

					COMPBAND c;
					c.from = X;
					c.to = ot;
					c.comp = bb.comp;
					b.insert(b.begin() + ib + 1, c);

					reset();
					Redraw();
					return;
				}
				if (tcmd == 91 && b.size() > 1)
				{
					b.erase(b.begin() + ib);
					if (b.size() == 1)
					{
						b[0].from = 0;
						b[0].to = 1;
					}
					else
						if (ib == 0)
						{
							b[0].from = 0;
						}
						else
							if (ib == b.size())
							{
								b[b.size() - 1].to = 1.0f;
							}
							else
							{
		//						b[ib].from = b[ib - 1].to;
								b[ib - 1].to = b[ib].from;
							}
					reset();
					Redraw();
					return;
				}

			}
		}
	}

	virtual void LeftDoubleClick(WPARAM ww, LPARAM ll)
	{
		float x = 0, y = 0;
		x = (FLOAT)LOWORD(ll);
		y = (FLOAT)HIWORD(ll);
	}


	virtual void LeftDown(WPARAM ww, LPARAM ll)
	{
		wchar_t rr[1000] = {};
		float x = 0, y = 0;
		x = (FLOAT)LOWORD(ll);
		y = (FLOAT)HIWORD(ll);

		if (ChainHit(ww, ll))
			return;


		if (ShowDataMode > 0)
		{
			D2D1_RECT_F r = rc;
			r.top = rc.bottom - 2;
			r.bottom = r.top + 4;
			if (InRect<>(r, x, y))
			{
				SetCursor(ResizeCursorNS);
				ChangingDataSize = 1;
				return;
			}
		}


		// chain threshold
		for (auto& ch : Chains)
		{
			if (ch.A && InRect<>(ch.thres_rect, x, y))
			{
				SetCursor(ResizeCursorNS);
				ChangingChainThreshold = &ch;
				return;
			}
		}


		// Threshold hit test
		for (size_t ib = 0; ib < b.size(); ib++)
		{
			auto& bb = b[ib];
			if (!InRect<>(bb.BandRect, x, y))
				continue;

			if (InRect<>(bb.ThresholdRect, x, y))
			{
				SetCursor(ResizeCursorNS);
				ChangingThreshold = &bb;
				return;
			}
			if (InRect<>(bb.HysteresisRect, x, y))
			{
				SetCursor(ResizeCursorNS);
				ChangingHysteresis = &bb;
				return;
			}

			// buttons
			if (InRect<>(bb.ButtThres, x, y))
			{
				swprintf_s(rr, 1000, L"%.1f", bb.comp.threshold);
				if (!AskText(hParent, L"Threshold", L"Enter threshold (dB):", rr))
					return;
				bb.comp.threshold = (float)_wtof(rr);
				if (bb.comp.threshold > 0)
					bb.comp.threshold = -bb.comp.threshold;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRatioHyst, x, y) && bb.comp.Mode != COMPMODE::GATE)
			{
				swprintf_s(rr, 1000, L"%.1f", bb.comp.ratio);
				if (!AskText(hParent, L"Ratio", L"Enter ratio:", rr))
					return;
				bb.comp.ratio = (float)_wtof(rr);
				if (bb.comp.ratio <= 1)
					bb.comp.ratio = 1;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRatioHyst, x, y) && bb.comp.Mode == COMPMODE::GATE)
			{
				swprintf_s(rr, 1000, L"%.1f", bb.comp.hysteresis);
				if (!AskText(hParent, L"Hysteresis", L"Enter hysteresis (dB):", rr))
					return;
				bb.comp.hysteresis = (float)_wtof(rr);
				if (bb.comp.hysteresis >= bb.comp.threshold)
					bb.comp.hysteresis = bb.comp.threshold;
				Redraw();
				return;
			}

			if (InRect<>(bb.ButtAttack, x, y))
			{
				swprintf_s(rr, 1000, L"%i", (int)(1000 * bb.comp.attack));
				if (!AskText(hParent, L"Attack", L"Enter attack (ms):", rr))
					return;
				bb.comp.attack = _wtoi(rr) / 1000.0f;
				if (bb.comp.attack <= 0)
					bb.comp.attack = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRelease, x, y))
			{
				swprintf_s(rr, 1000, L"%i", (int)(1000 * bb.comp.release));
				if (!AskText(hParent, L"Release", L"Enter release (ms):", rr))
					return;
				bb.comp.release = _wtoi(rr) / 1000.0f;
				if (bb.comp.release <= 0)
					bb.comp.release = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtHold, x, y))
			{
				swprintf_s(rr, 1000, L"%i", (int)(1000 * bb.comp.hold));
				if (!AskText(hParent, L"Hold", L"Enter hold (ms):", rr))
					return;
				bb.comp.hold = _wtoi(rr) / 1000.0f;
				if (bb.comp.hold <= 0)
					bb.comp.hold = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtMakeup, x, y))
			{
				swprintf_s(rr, 1000, L"%.1f", bb.comp.makeup);
				if (!AskText(hParent, L"Make up gain", L"Enter make up gain (dB):", rr))
					return;
				bb.comp.makeup = (float)_wtof(rr);
				if (bb.comp.makeup <= 0)
					bb.comp.makeup = 0;
				Redraw();
				return;
			}

			// ratio hit test
			D2D1_RECT_F re = { x - 3,y - 3,x + 3,y + 3 };
			bool RHT = false;
			auto rht1 = lineLineIntersection({ re.left,re.top }, { re.right,re.bottom }, { bb.RatioPoints2.left, bb.RatioPoints2.top }, { bb.RatioPoints2.right,bb.RatioPoints2.bottom });
			if (fabs(rht1.x - x) < 3 && fabs(rht1.y - y) < 3)
			{
				RHT = true;
			}
			if (RHT)
			{
				SetCursor(ResizeCursorNS);
				ChangingRatio = &bb;
				return;
			}

			// multiband line 
			if (b.size() > 1 && ib != b.size() - 1)
			{
				float vertX = Freq2X(bb.to * MaxHz);
				D2D1_RECT_F r3 = rc;
				r3.left = vertX - 3;
				r3.right = vertX + 3;
				if (InRect<>(r3, x, y))
				{
					SetCursor(ResizeCursorEW);
					ChangingRight = &bb;
					ChangingLeft = &b[ib + 1];
					return;
				}

			}

		}
	}


	virtual void MouseWheel(WPARAM ww, LPARAM ll)
	{
		float x = 0, y = 0;
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(hParent, &pt);
		x = (FLOAT)pt.x;
		y = (FLOAT)pt.y;
		signed short HW = HIWORD(ww);
		bool Shift = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

		for (size_t ib = 0; ib < b.size(); ib++)
		{
			auto& bb = b[ib];
			// buttons
			if (InRect<>(bb.ButtThres, x, y))
			{
				if (Shift)
				{
					if (HW > 0)
						bb.comp.threshold += 0.1f;
					else
						bb.comp.threshold -= 0.1f;
				}
				else
				{
					if (HW > 0)
						bb.comp.threshold += 1;
					else
						bb.comp.threshold -= 1;
				}
				if (bb.comp.threshold > 0)
					bb.comp.threshold = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRatioHyst, x, y) && bb.comp.Mode != COMPMODE::GATE)
			{
				if (HW > 0)
					bb.comp.ratio += 0.1f;
				else
					bb.comp.ratio -= 0.1f;
				if (bb.comp.ratio < 1)
					bb.comp.ratio = 1;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRatioHyst, x, y) && bb.comp.Mode == COMPMODE::GATE)
			{
				if (Shift)
				{
					if (HW > 0)
						bb.comp.hysteresis += 0.1f;
					else
						bb.comp.hysteresis -= 0.1f;
				}
				else
				{
					if (HW > 0)
						bb.comp.hysteresis += 1;
					else
						bb.comp.hysteresis -= 1;
				}
				if (bb.comp.hysteresis > bb.comp.threshold)
					bb.comp.hysteresis = bb.comp.threshold;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtAttack, x, y))
			{
				if (HW > 0)
					bb.comp.attack += 0.1f;
				else
					bb.comp.attack -= 0.1f;
				if (bb.comp.attack < 0)
					bb.comp.attack = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtHold, x, y))
			{
				if (HW > 0)
					bb.comp.hold += 0.1f;
				else
					bb.comp.hold -= 0.1f;
				if (bb.comp.hold < 0)
					bb.comp.hold = 0;
				Redraw();
				return;
			}
			if (InRect<>(bb.ButtRelease, x, y))
			{
				if (HW > 0)
					bb.comp.release += 0.1f;
				else
					bb.comp.release -= 0.1f;
				if (bb.comp.release < 0)
					bb.comp.release = 0;
				Redraw();
				return;
			}

			if (InRect<>(bb.ButtMakeup, x, y))
			{
				if (HW > 0)
					bb.comp.makeup += 1.0f;
				else
					bb.comp.makeup -= 1.0f;
				if (bb.comp.makeup < 0)
					bb.comp.makeup = 0;
				Redraw();
				return;
			}
		}
	}

	HCURSOR ArrowCursor = LoadCursor(0, IDC_ARROW);
	HCURSOR ResizeCursorNS = LoadCursor(0, IDC_SIZENS);
	HCURSOR ResizeCursorEW = LoadCursor(0, IDC_SIZEWE);

	COMPCHAIN* ChangingChainThreshold = 0;
	COMPBAND* ChangingThreshold = 0;
	COMPBAND* ChangingHysteresis = 0;
	COMPBAND* ChangingRatio = 0;
	COMPBAND* ChangingLeft = 0;
	COMPBAND* ChangingRight = 0;
	virtual void MouseMove(WPARAM ww, LPARAM ll)
	{
		bool LeftClick = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);

		float x = 0, y = 0;
		x = (FLOAT)LOWORD(ll);
		y = (FLOAT)HIWORD(ll);

		if (LeftClick)
		{
			if (ChangingDataSize)
			{
				RECT rc3 = {};
				GetClientRect(PaintWindow, &rc3);
				DataSize =(int)(rc3.bottom - y);
				SetCursor(ResizeCursorNS);
				Redraw();
				return;
			}
			if (ChangingChainThreshold)
			{
				SetCursor(ResizeCursorNS);
				ChangingChainThreshold->thr = V2DB(Y2V(y));
				if (ChangingChainThreshold->thr < -96)
					ChangingChainThreshold->thr = -96;
				Redraw();
				return;
			}

			if (ChangingThreshold)
			{
				SetCursor(ResizeCursorNS);
				ChangingThreshold->comp.threshold = V2DB(Y2V(y));
				if (ChangingThreshold->comp.threshold < -96)
					ChangingThreshold->comp.threshold = -96;
				ChangingThreshold->comp.hysteresis = ChangingThreshold->comp.threshold - 3;
				Redraw();
				return;
			}
			if (ChangingHysteresis && ChangingHysteresis->comp.Mode == COMPMODE::GATE)
			{
				SetCursor(ResizeCursorNS);
				ChangingHysteresis->comp.hysteresis = V2DB(Y2V(y));
				if (ChangingHysteresis->comp.hysteresis >= (ChangingHysteresis->comp.threshold - 1))
					ChangingHysteresis->comp.hysteresis = ChangingHysteresis->comp.threshold - 1;
				Redraw();
				return;
			}
			if (ChangingRatio)
			{
				SetCursor(ResizeCursorNS);
				// pt2 and xy -> edge, y = new db
				float slope = (ChangingRatio->RatioPoints2.top - y) / (ChangingRatio->RatioPoints2.left - x);
				// y = m(x-x1)+y1
				float vertX = (float)Freq2X(ChangingRatio->to * MaxHz);

				float yy = slope * (vertX - ChangingRatio->RatioPoints2.left) + ChangingRatio->RatioPoints2.top;

				//float dbn = bb.comp.threshold + (-bb.comp.threshold / bb.comp.ratio);
				// r = 
				auto dbn = V2DB(Y2V(yy));
				if (yy > rc.bottom)
					dbn = -72;

				ChangingRatio->comp.ratio = ChangingRatio->comp.threshold / (ChangingRatio->comp.threshold - dbn);
//				dbn = bb.comp.threshold * bb.comp.ratio;

				if (ChangingRatio->comp.Mode == COMPMODE::DEXPANDER)
					ChangingRatio->comp.ratio = dbn / ChangingRatio->comp.threshold;
				if (ChangingRatio->comp.Mode == COMPMODE::UEXPANDER)
					ChangingRatio->comp.ratio = ChangingRatio->comp.threshold/dbn;
				if (ChangingRatio->comp.Mode == COMPMODE::UDEXPANDER)
					ChangingRatio->comp.ratio = ChangingRatio->comp.threshold / dbn;
				if (fabs(ChangingRatio->comp.ratio) > 100)
					ChangingRatio->comp.ratio = 100;
				if (ChangingRatio->comp.ratio < 1 && ChangingRatio->comp.ratio > 0)
					ChangingRatio->comp.ratio = 1.0;
				if (ChangingRatio->comp.ratio < 0)
					ChangingRatio->comp.ratio = 100;
				Redraw();
				return;
			}
			if (ChangingLeft && ChangingRight)
			{
				float nhz = (float)X2Freqr(x);
				ChangingRight->to = nhz / (float)MaxHz;
				ChangingLeft->from = nhz / (float)MaxHz;
				reset();
				Redraw();
				return;
			}
		}

		if (ShowDataMode > 0)
		{
			D2D1_RECT_F r = rc;
			r.top = rc.bottom - 2;
			r.bottom = r.top + 4;
			if (InRect<>(r, x, y))
			{
				SetCursor(ResizeCursorNS);
				Redraw();
				return;
			}
		}

		for (auto& bb : b)
		{
			if (!InRect<>(bb.BandRect, x, y))
				continue;

			// chain threshold
			for (auto& ch : Chains)
			{
				if (ch.A && InRect<>(ch.thres_rect, x, y))
				{
					SetCursor(ResizeCursorNS);
					return;

				}
			}

			// Threshold hit test
			if (InRect<>(bb.ThresholdRect, x, y))
			{
				SetCursor(ResizeCursorNS);
				return;
			}

			// Hy hit test
			if (InRect<>(bb.HysteresisRect, x, y) && bb.comp.Mode == COMPMODE::GATE)
			{
				SetCursor(ResizeCursorNS);
				return;
			}

			// ratio hit test
			D2D1_RECT_F re = { x - 3,y - 3,x + 3,y + 3 };
			bool RHT = false;
			auto rht1 = lineLineIntersection({ re.left,re.top }, { re.right,re.bottom }, { bb.RatioPoints2.left, bb.RatioPoints2.top }, { bb.RatioPoints2.right,bb.RatioPoints2.bottom });
			if (fabs(rht1.x - x) < 3 && fabs(rht1.y - y) < 3)
			{
				RHT = true;
			}
			if (RHT)
			{
				SetCursor(ResizeCursorNS);
				return;
			}

			// multiband line 
			if (b.size() > 1)
			{
				float vertX = Freq2X(bb.to * MaxHz);
				D2D1_RECT_F r3 = rc;
				r3.left = vertX - 3;
				r3.right = vertX + 3;
				if (InRect<>(r3, x, y))
				{
					SetCursor(ResizeCursorEW);
					return;

				}

			}
		}



		SetCursor(ArrowCursor);

	}

	virtual void LeftUp(WPARAM ww, LPARAM ll)
	{
		ChangingDataSize = 0;
		ChangingChainThreshold = 0;
		ChangingThreshold = 0;
		ChangingHysteresis = 0;
		ChangingRatio = 0;
		ChangingLeft = 0;
		ChangingRight = 0;
	}

	virtual void KeyDown(WPARAM ww, LPARAM ll)
	{
	}

	int TheSR = 0;
	int LastBuildChannel = 0;
	void Build(int SR, int ns,int nch)
	{
		if (LastBuildChannel != nch)
		{
			// All filters off
			for (auto& bb : b)
			{
				bb.sf = 0;
			}
		}
		LastBuildChannel = nch;
		std::lock_guard<std::recursive_mutex> lg(mu);
		TheSR = SR;
		// Filter x num
		for (size_t ib = 0; ib < b.size(); ib++)
		{
			auto& bb = b[ib];
#ifdef DYNAMIC_CHANNEL
			bb.din.resize(nch);
			bb.dout.resize(nch);
			for (auto& d : bb.din)
				d.resize(ns);
			for (auto& d : bb.dout)
				d.resize(ns);
#else
			bb.in[0].resize(ns);
			bb.in[1].resize(ns);
			bb.out[0].resize(ns);
			bb.out[1].resize(ns);
#endif

			if (ib == 0)
			{
				// Low pass
				double fr = bb.to * MaxHz;
				std::shared_ptr_debug<void>& sf = bb.sf;
				if (!sf)
				{
					if (bb.Type == 0)
					{
						auto sf2 = std::make_shared_debug<ButtLP>();
						sf2->setNumChannels(nch);
						sf2->setup(bb.Order, SR, fr);
						sf = sf2;
					}
					if (bb.Type == 1)
					{
						auto sf2 = std::make_shared_debug<Che1LP>();
						sf2->setNumChannels(nch);
						sf2->setup(bb.Order, SR, fr, bb.ripple);
						sf = sf2;
					}
					if (bb.Type == 2)
					{
						auto sf2 = std::make_shared_debug<Che2LP>();
						sf2->setNumChannels(nch);
						sf2->setup(bb.Order, SR, fr, bb.ripple);
						sf = sf2;
					}
					if (bb.Type == 3)
					{
						auto sf2 = std::make_shared_debug<EllLP>();
						sf2->setNumChannels(nch);
						sf2->setup(bb.Order, SR, fr, bb.ripple, bb.rolloff);
						sf = sf2;
					}
				}
			}
			else
				if (ib == b.size() - 1)
				{
					// High pass
					double fr = bb.from * MaxHz;
					std::shared_ptr_debug<void>& sf = bb.sf;
					if (!sf)
					{
						if (bb.Type == 0)
						{
							auto sf2 = std::make_shared_debug<ButtHP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr);
							sf = sf2;
						}
						if (bb.Type == 1)
						{
							auto sf2 = std::make_shared_debug<Che1HP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr, bb.ripple);
							sf = sf2;
						}
						if (bb.Type == 2)
						{
							auto sf2 = std::make_shared_debug<Che2HP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr, bb.ripple);
							sf = sf2;
						}
						if (bb.Type == 3)
						{
							auto sf2 = std::make_shared_debug<EllHP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr, bb.ripple, bb.rolloff);
							sf = sf2;
						}
					}
				}
				else
				{
					// Bandpass filter
					double fr1 = bb.from * MaxHz;
					double fr2 = bb.to * MaxHz;
					std::shared_ptr_debug<void>& sf = bb.sf;
					if (!sf)
					{
						if (bb.Type == 0)
						{
							auto sf2 = std::make_shared_debug<ButtBP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr1 + (fr2 - fr1) / 2.0, (fr2 - fr1) / 2.0);
							sf = sf2;
						}
						if (bb.Type == 1)
						{
							auto sf2 = std::make_shared_debug<Che1BP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr1 + (fr2 - fr1) / 2.0, (fr2 - fr1) / 2.0, bb.ripple);
							sf = sf2;
						}
						if (bb.Type == 2)
						{
							auto sf2 = std::make_shared_debug<Che2BP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr1 + (fr2 - fr1) / 2.0, (fr2 - fr1) / 2.0, bb.ripple);
							sf = sf2;
						}
						if (bb.Type == 3)
						{
							auto sf2 = std::make_shared_debug<EllBP>();
							sf2->setNumChannels(nch);
							sf2->setup(bb.Order, SR, fr1 + (fr2 - fr1) / 2.0, (fr2 - fr1) / 2.0, bb.ripple, bb.rolloff);
							sf = sf2;
						}
					}
				}
		}

	}


#ifndef DYNAMIC_CHANNEL
	std::vector<float> empty1;
	std::vector<float> empty2;
#endif
	void process(int SR, int nch,float** inputs, int ns, float** outputs, std::vector<std::vector<float>>* chainin, bool ForceUnTrigger)
	{
		if (nch == 0 || inputs == 0 || outputs == 0 || ns == 0)
			return;
		if (nch != LastBuildChannel)
			Build(SR, ns, nch);
		Ensure();
		bool M = false;
		if (!Single())
			M = true;


		if (ShowDataMode > 0 && IsWindow(PaintWindow))
		{
			std::lock_guard<std::recursive_mutex> lg(mu);
			dins.resize(nch);
			int NeedSamples = SR * 4;
			for (int i = 0; i < nch; i++)
			{
				auto& din = dins[i];
				auto sz = din.size();
				if (sz <= NeedSamples)
					din.resize(NeedSamples);
				sz = din.size();
				din.resize(sz + ns);
				memcpy(din.data() + sz, inputs[i], ns * sizeof(float));
				if (din.size() > NeedSamples)
				{
					auto rd = din.size() - NeedSamples;
					din.erase(din.begin(), din.begin() + rd);
					sz = din.size();
				}
			}
		}

		//* triggering
		bool Trigger = 0;
		if (!chainin)
			Trigger = 1;
		if (ForceUnTrigger)
			Trigger = 0;
		if (chainin || ForceUnTrigger)
		{
			if (chainin)
			{
				for (size_t ic = 0; ic < Chains.size(); ic++)
				{
					if (chainin->size() <= ic)
						break;
					if ((*chainin)[ic].size() < ns)
						continue;
					auto lin = fabs(db2lin(Chains[ic].thr));
					for (int i = 0; i < ns; i++)
					{
						float s = fabs((*chainin)[ic][i]);
						if (s >= lin)
						{
							if (LastInputDBChain < lin)
								LastInputDBChain = lin;
							Trigger = 1;
						}
					}
				}
			}

			if (!Trigger && !M)
			{
				auto& bb = b[0];
				if (bb.comp.state == COMPSTATE::NONE || bb.comp.state == COMPSTATE::ATTACK)
				{
					bb.comp.state = COMPSTATE::NONE;
					for(int ich = 0 ; ich < nch ; ich++)
						memcpy(outputs[ich], inputs[ich], ns * sizeof(float));

					// Detect the db
					float max_sample = 0;
					for (int i = 0; i < ns; i++)
					{
						float s = fabs(inputs[0][i]);
						if (max_sample < s)
							max_sample = s;
					}
					float db = lin2db(max_sample);
					bb.LastInputDB = db;
					return;
				}
			}
		}

		// Setup the filters
		if (M)
		{
			Build(SR, ns,nch);
		}


		if (M)
		{
#ifdef DYNAMIC_CHANNEL
			for (size_t ib = 0; ib < b.size(); ib++)
			{
				auto& bb = b[ib];
				float* ins[100] = { 0 }; // 100ch max :)
				for (int ich = 0; ich < nch; ich++)
				{
					ins[ich] = bb.din[ich].data();
					memcpy(ins[ich], inputs[ich], ns * sizeof(float));
				}


				if (ib == 0) // Low Pass
				{
					if (bb.Type == 0)
					{
						std::shared_ptr_debug<ButtLP> fx;
						fx = std::static_pointer_cast<ButtLP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 1)
					{
						std::shared_ptr_debug<Che1LP> fx;
						fx = std::static_pointer_cast<Che1LP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 2)
					{
						std::shared_ptr_debug<Che2LP> fx;
						fx = std::static_pointer_cast<Che2LP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 3)
					{
						std::shared_ptr_debug<EllLP> fx;
						fx = std::static_pointer_cast<EllLP>(bb.sf);
						fx->process(ns, ins);
					}
				}
				else
				if (ib == b.size() - 1) // high Pass
				{
					if (bb.Type == 0)
					{
						std::shared_ptr_debug<ButtHP> fx;
						fx = std::static_pointer_cast<ButtHP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 1)
					{
						std::shared_ptr_debug<Che1HP> fx;
						fx = std::static_pointer_cast<Che1HP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 2)
					{
						std::shared_ptr_debug<Che2HP> fx;
						fx = std::static_pointer_cast<Che2HP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 3)
					{
						std::shared_ptr_debug<EllHP> fx;
						fx = std::static_pointer_cast<EllHP>(bb.sf);
						fx->process(ns, ins);
					}
				}
				else // Bandpass
				{
					if (bb.Type == 0)
					{
						std::shared_ptr_debug<ButtBP> fx;
						fx = std::static_pointer_cast<ButtBP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 1)
					{
						std::shared_ptr_debug<Che1BP> fx;
						fx = std::static_pointer_cast<Che1BP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 2)
					{
						std::shared_ptr_debug<Che2BP> fx;
						fx = std::static_pointer_cast<Che2BP>(bb.sf);
						fx->process(ns, ins);
					}
					if (bb.Type == 3)
					{
						std::shared_ptr_debug<EllBP> fx;
						fx = std::static_pointer_cast<EllBP>(bb.sf);
						fx->process(ns, ins);
					}
				}
			}
#else // stereo
			// Process multi-channel filters
			empty1.resize(ns);
			empty2.resize(ns);
			for (int ich = 0; ich < nch; ich += 2)
			{
				auto input1 = inputs[ich];
				auto input2 = (ich == (nch - 1) ? empty1.data() : inputs[ich + 1]);
				for (size_t ib = 0; ib < b.size(); ib++)
				{
					auto& bb = b[ib];
					float* ins[2];
					ins[0] = bb.in[0].data();
					ins[1] = bb.in[1].data();

					memcpy(ins[0], input1, ns * sizeof(float));
					memcpy(ins[1], input2, ns * sizeof(float));

					if (ib == 0) // Low Pass
					{
						if (bb.Type == 0)
						{
							std::shared_ptr_debug<ButtLP> fx;
							fx = std::static_pointer_cast<ButtLP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 1)
						{
							std::shared_ptr_debug<Che1LP> fx;
							fx = std::static_pointer_cast<Che1LP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 2)
						{
							std::shared_ptr_debug<Che2LP> fx;
							fx = std::static_pointer_cast<Che2LP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 3)
						{
							std::shared_ptr_debug<EllLP> fx;
							fx = std::static_pointer_cast<EllLP>(bb.sf);
							fx->process(ns, ins);
						}
					}
					else
					if (ib == b.size() - 1) // high Pass
					{
						if (bb.Type == 0)
						{
							std::shared_ptr_debug<ButtHP> fx;
							fx = std::static_pointer_cast<ButtHP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 1)
						{
							std::shared_ptr_debug<Che1HP> fx;
							fx = std::static_pointer_cast<Che1HP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 2)
						{
							std::shared_ptr_debug<Che2HP> fx;
							fx = std::static_pointer_cast<Che2HP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 3)
						{
							std::shared_ptr_debug<EllHP> fx;
							fx = std::static_pointer_cast<EllHP>(bb.sf);
							fx->process(ns, ins);
						}
					}
					else // Bandpass
					{
						if (bb.Type == 0)
						{
							std::shared_ptr_debug<ButtBP> fx;
							fx = std::static_pointer_cast<ButtBP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 1)
						{
							std::shared_ptr_debug<Che1BP> fx;
							fx = std::static_pointer_cast<Che1BP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 2)
						{
							std::shared_ptr_debug<Che2BP> fx;
							fx = std::static_pointer_cast<Che2BP>(bb.sf);
							fx->process(ns, ins);
						}
						if (bb.Type == 3)
						{
							std::shared_ptr_debug<EllBP> fx;
							fx = std::static_pointer_cast<EllBP>(bb.sf);
							fx->process(ns, ins);
						}
					}
				}
			}
#endif
		}

		for (int ich = 0; ich < nch; ich++)
		{
			auto input = inputs[ich];
			auto output = outputs[ich];
			for (size_t ib = 0; ib < b.size(); ib++)
			{
				float* d = input;
				float* o = output;
				auto& bb = b[ib];
				if (M)
				{
					d = bb.din[ich].data();
					o = bb.dout[ich].data();
				}


				int AttackSamples = (int)(SR * bb.comp.attack);
				int ReleaseSamples = (int)(SR * bb.comp.release);
				int HoldSamples = (int)(SR * bb.comp.hold);

				// Detect the db
				float max_sample = 0;
				for (int i = 0; i < ns; i++)
				{
					float s = fabs(d[i]);
					if (max_sample < s)
						max_sample = s;
				}
				float db = lin2db(max_sample);
				bb.LastInputDB = db;



				if (bb.comp.Mode == COMPMODE::GATE)
				{
					for (int i = 0; i < ns; i++)
					{
						if (bb.comp.state == COMPSTATE::NONE)
						{
							// Gate closed
							if (db < bb.comp.threshold)
							{
								o[i] = 0;
								continue;
							}
							else
							{
								// Create an attack
								bb.comp.state = COMPSTATE::ATTACK;
								bb.comp.SamplesLeftforProgress = AttackSamples;
							}
						}

						if (bb.comp.state == COMPSTATE::ATTACK)
						{
							if (db < bb.comp.hysteresis)
							{
								o[i] = 0;
								bb.comp.state = COMPSTATE::NONE;
								continue;
							}

							if (bb.comp.SamplesLeftforProgress == 0)
								bb.comp.state = COMPSTATE::ACTION;
							else
							{
								// slow to full 
								float lin1 = db2lin(bb.comp.hysteresis);
								// In ReleaseSamples, lin1
								// in remain samples , ? 
								float lin2 = lin1 * (AttackSamples - bb.comp.SamplesLeftforProgress) / (float)AttackSamples;
								o[i] = (lin2 / lin1) * d[i];
								bb.comp.SamplesLeftforProgress--;
								continue;
							}
						}

						if (bb.comp.state == COMPSTATE::RELEASE)
						{
							if (db > bb.comp.threshold)
							{
								bb.comp.state = COMPSTATE::ACTION;
								continue;
							}
							if (bb.comp.SamplesLeftforProgress == 0)
								bb.comp.state = COMPSTATE::NONE;
							else
							{
								// full to slow
								float lin1 = db2lin(bb.comp.hysteresis);
								// In ReleaseSamples, lin1
								// in remain samples , ? 
								float lin2 = lin1 * bb.comp.SamplesLeftforProgress / (float)ReleaseSamples;
								o[i] = (lin2 / lin1) * d[i];
								bb.comp.SamplesLeftforProgress--;
								continue;
							}
						}

						if (bb.comp.state == COMPSTATE::HOLD)
						{
							if (db >= bb.comp.threshold)
								bb.comp.state = COMPSTATE::ACTION;
							else
							{
								if (bb.comp.SamplesLeftforProgress == 0)
								{
									bb.comp.state = COMPSTATE::RELEASE;
									bb.comp.SamplesLeftforProgress = ReleaseSamples;
								}
								else
								{
									bb.comp.SamplesLeftforProgress--;
									o[i] = d[i];
									continue;
								}
							}
						}


						if (bb.comp.state == COMPSTATE::ACTION)
						{
							d[i] = o[i];
							if (db < bb.comp.hysteresis)
							{
								bb.comp.state = COMPSTATE::HOLD;
								bb.comp.SamplesLeftforProgress = HoldSamples;
							}
						}
					}
				}
				else
				{
					bool ShouldRun = 0;
					if (db < bb.comp.threshold && bb.comp.Mode == COMPMODE::DEXPANDER)
						ShouldRun = 1;
					if (db > bb.comp.threshold && bb.comp.Mode == COMPMODE::UEXPANDER)
						ShouldRun = 1;
					if (bb.comp.Mode == COMPMODE::UDEXPANDER)
						ShouldRun = 1;
					if (db > bb.comp.threshold && bb.comp.Mode == COMPMODE::COMPRESSOR)
						ShouldRun = 1;

					if (!ShouldRun || !Trigger)
					{
						if (bb.comp.state == COMPSTATE::ACTION)
						{
							// Hold
							bb.comp.state = COMPSTATE::HOLD;
							bb.comp.SamplesLeftforProgress = HoldSamples;

							if (bb.comp.SamplesLeftforProgress == 0)
							{
								// Start Release
								bb.comp.state = COMPSTATE::RELEASE;
								bb.comp.SamplesLeftforProgress = ReleaseSamples;

								if (bb.comp.SamplesLeftforProgress == 0)
								{
									bb.comp.state = COMPSTATE::NONE;
									bb.comp.SamplesLeftforProgress = 0;
								}
							}
						}
						else
							if (bb.comp.state == COMPSTATE::RELEASE || bb.comp.state == COMPSTATE::HOLD)
							{

							}
							else
							{
								bb.comp.state = COMPSTATE::NONE;
								bb.comp.SamplesLeftforProgress = 0;
							}
					}
					else
					{
						if (bb.comp.state == COMPSTATE::NONE)
						{
							bb.comp.SamplesLeftforProgress = AttackSamples;
							bb.comp.state = COMPSTATE::ATTACK;
							if (AttackSamples == 0)
								bb.comp.state = COMPSTATE::ACTION;
						}
					}

					if (ShouldRun && Trigger && bb.comp.state == COMPSTATE::HOLD)
					{
						bb.comp.state = COMPSTATE::ACTION;
						bb.comp.SamplesLeftforProgress = 0;
					}
					else
						if (ShouldRun && Trigger && bb.comp.state == COMPSTATE::RELEASE)
						{
							bb.comp.state = COMPSTATE::ACTION;
							bb.comp.SamplesLeftforProgress = 0;
						}



					if (bb.comp.state == COMPSTATE::NONE && !M)
					{
						memcpy(o, d, ns * sizeof(float));
					}
					else
					{
						// Calculate R
						for (int i = 0; i < ns; i++)
						{
							float R = 1.0f;
							if (bb.comp.state == COMPSTATE::ATTACK)
							{
								// in ratio, AttackSamples
								// ?       , StateSamples
								R = (bb.comp.ratio - 1) * (AttackSamples - bb.comp.SamplesLeftforProgress) / AttackSamples;
								R += 1;
							}
							if (bb.comp.state == COMPSTATE::ACTION || bb.comp.state == COMPSTATE::HOLD)
								R = bb.comp.ratio;
							if (bb.comp.state == COMPSTATE::RELEASE)
							{
								R = (bb.comp.ratio - 1) * (bb.comp.SamplesLeftforProgress) / ReleaseSamples;
								R += 1;
							}

							// For drawing
							if (bb.comp.Mode == COMPMODE::COMPRESSOR)
							{
								float diff = bb.comp.threshold - bb.LastInputDB;
								diff *= R;
								float db2 = bb.comp.threshold - diff;
								bb.LastInputDBC = db2;
							}

							if (bb.comp.Mode == COMPMODE::DEXPANDER || (bb.comp.Mode == COMPMODE::UDEXPANDER && db < bb.comp.threshold))
							{
								auto fax = d[i];
								float diff = bb.comp.threshold - db;
								diff *= R;
								float db2 = bb.comp.threshold - diff;
								auto f1 = db2lin(db);
								auto f2 = db2lin(db2);
								float VV = f2 / f1;
								fax *= VV;
								o[i] = fax;
							}
							else
								if (bb.comp.Mode == COMPMODE::UEXPANDER || (bb.comp.Mode == COMPMODE::UDEXPANDER && db > bb.comp.threshold))
								{
									auto fax = d[i];
									float diff = db - bb.comp.threshold;
									diff *= R;
									float db2 = bb.comp.threshold + diff;
									auto f1 = db2lin(db);
									auto f2 = db2lin(db2);
									float VV = f2 / f1;
									fax *= VV;
									o[i] = fax;
								}
								else
									if (bb.comp.Mode == COMPMODE::COMPRESSOR)
									{
										auto fax = d[i];
										float diff = db - bb.comp.threshold;
										diff /= R;
										float db2 = bb.comp.threshold + diff;
										auto f1 = db2lin(db);
										auto f2 = db2lin(db2);
										float VV = f2 / f1;
										fax *= VV;
										o[i] = fax;
									}

							if (bb.comp.makeup)
							{
								auto dbc = lin2db(fabs(o[i]));
								dbc += bb.comp.makeup;
								if (o[i] < 0)
									o[i] = -db2lin(dbc);
								else
									o[i] = db2lin(dbc);
							}


							bb.comp.SamplesLeftforProgress--;
							if (bb.comp.SamplesLeftforProgress == 0)
							{
								if (bb.comp.state == COMPSTATE::HOLD)
								{
									bb.comp.state = COMPSTATE::RELEASE;
									bb.comp.SamplesLeftforProgress = ReleaseSamples;
								}
								else
									if (bb.comp.state == COMPSTATE::RELEASE)
										bb.comp.state = COMPSTATE::NONE;
									else
										if (bb.comp.state == COMPSTATE::ATTACK)
											bb.comp.state = COMPSTATE::ACTION;
							}
						}
						if (bb.comp.AutoDeclipper)
						{
							float vm = 0;
							for (int fi = 0; fi < ns; fi++)
							{
								auto fb = fabs(o[fi]);
								if (fb > vm)
									vm = fb;
							}
							if (vm > 1.0f)
							{
								for (int fi = 0; fi < ns; fi++)
									o[fi] /= vm;
							}
						}
					}
				}
			}
		}

		if (M)
		{
			for (int ich = 0; ich < nch; ich++)
			{
				auto output = outputs[ich];
				for (int i = 0; i < ns; i++)
				{
					output[i] = 0;
					for (auto& bb : b)
						output[i] += bb.dout[ich][i];
					if (globalmakeup)
					{
						auto dbc = lin2db(fabs(output[i]));
						dbc += globalmakeup;
						if (output[i] < 0)
							output[i] = -db2lin(dbc);
						else
							output[i] = db2lin(dbc);
					}
				}
			}
		}

		if (ShowDataMode > 0 && IsWindow(PaintWindow))
		{
			std::lock_guard<std::recursive_mutex> lg(mu);
			douts.resize(nch);
			int NeedSamples = SR * 4;
			for (int i = 0; i < nch; i++)
			{
				auto& din = douts[i];
				auto sz = din.size();
				if (sz <= NeedSamples)
					din.resize(NeedSamples);
				sz = din.size();
				din.resize(sz + ns);
				memcpy(din.data() + sz, outputs[i], ns * sizeof(float));
				if (din.size() > NeedSamples)
				{
					auto rd = din.size() - NeedSamples;
					din.erase(din.begin(), din.begin() + rd);
				}
			}
		}

	}

	float dBFSToPercent(float dBFS = 0)
	{
		return 100.0f * (pow(10.0f, (dBFS / 20.0f)));
	}
	float PercentTodBFS(float P)
	{
		return 20.0f * log10(P / 100.0f);
	}

	// Linear frequencies here
	float X2Freqrli(float x)
	{
		// in len , MaxHz
		// in x,    ?
		return (float)MaxHz * x / (rc.right - rc.left);
	}
	float Freq2Xli(float f)
	{
		if (f == 0)
			return rc.left;

		// In len, MaxHz
		//   ?   ,  f
		return (float)(rc.right - rc.left) * f / MaxHz;
	}

	// Logarithmic
	float X2Freqrlg(float x)
	{
		return  (float)(exp(log(MaxHz) * x / (rc.right - rc.left)));
	}
	float Freq2Xlg(float f)
	{
		if (f == 0)
			return rc.left;
		return (float)((rc.right - rc.left) * log(f) / log(MaxHz));
	}

	int fmodelog = 1; // logarithmic == 1
	float Freq2X(float f)
	{
		if (fmodelog == 1)
			return Freq2Xlg(f);
		return Freq2Xli(f);
	}
	float X2Freqr(float f)
	{
		if (fmodelog == 1)
			return X2Freqrlg(f);
		return X2Freqrli(f);
	}
	float V2DB(float V, bool NeedNZ = 0)
	{
		float dBX = 0;
		if (V > 1.0f)
			return -96;
		dBX = PercentTodBFS((1.0f - V) * 100.0f);
		return dBX;
	}

	float dB2V(float dB)
	{
		if (dB >= 0)
			return 0;

		float p = dBFSToPercent(dB);;
		float V = p / 100.0f;
		return V;
	}

	float Y2V(float Y)
	{
		// In V=2, height
		// ?   , y
		Y -= 5;
		return Y / (rc.bottom - rc.top);
	}

	float V2Y(float V)
	{
		if (V == 0)
			return rc.top + 5;
		// In 2, height
		// in V,  ?
		float he = rc.bottom - rc.top;
		return he - (he)*V;
	}

	std::wstring HZString(double z)
	{
		wchar_t a[100] = { 0 };
		if (z < 1000)
			swprintf_s(a, 100, L"%.0f Hz", z);
		else
			swprintf_s(a, 100, L"%.0f KHz", z / 1000.0f);
		return a;
	}

	std::wstring HZString(double z1, double z2)
	{
		wchar_t a[100] = { 0 };
		if (z1 < 1000 && z2 < 1000)
			swprintf_s(a, 100, L"%.0f - %.0f Hz", z1, z2);
		else
			if (z1 < 1000 && z2 >= 1000)
				swprintf_s(a, 100, L"%.0f Hz - %.0f KHz", z1, z2 / 1000);
			else
				swprintf_s(a, 100, L"%.0f - %.0f KHz", z1 / 1000.0f, z2 / 1000.0f);
		return a;
	}



	void ShowInDialog(HWND hh, int SR)
	{
		struct Z
		{
			COMP* c = 0;
			int SR = 48000;
			CComPtr<ID2D1HwndRenderTarget> d;
			CComPtr<ID2D1Factory> fa;
		};

		const char* res = "\x01\x00\xFF\xFF\x00\x00\x00\x00\x00\x00\x00\x00\xC8\x08\xCF\x80\x00\x00\x00\x00\x00\x00\xC5\x02\x95\x01\x00\x00\x00\x00\x00\x00\x08\x00\x90\x01\x00\x01\x4D\x00\x53\x00\x20\x00\x53\x00\x68\x00\x65\x00\x6C\x00\x6C\x00\x20\x00\x44\x00\x6C\x00\x67\x00\x00\x00";

		auto dp = [](HWND hh, UINT mm, WPARAM ww, LPARAM ll) -> INT_PTR
		{
			Z* z = (Z*)GetWindowLongPtr(hh, GWLP_USERDATA);
			COMP* c = 0;
			if (z)
				c = z->c;

			switch (mm)
			{
				case WM_TIMER:
				{
					InvalidateRect(hh, 0, true);
					UpdateWindow(hh);
					return 0;
				}


				case WM_INITDIALOG:
				{
					SetWindowLongPtr(hh, GWLP_USERDATA, ll);
					z = (Z*)GetWindowLongPtr(hh, GWLP_USERDATA);
					c = z->c;


					c->SetWindow(hh);
					auto mmd = std::make_shared_debug<MMCB>();
					mmd->hC = hh;
					mmd->SR = z->SR;
					c->AddCallback(mmd);
					SetTimer(hh, 1, 100, 0);
					return true;
				}
				case WM_CLOSE:
				{
					EndDialog(hh, 0);
					return 0;
				}
				case WM_KEYDOWN:
				case WM_SYSKEYDOWN:
				{
					c->KeyDown(ww, ll);
					return 0;
				}

				case WM_MOUSEMOVE:
				{
					c->MouseMove(ww, ll);
					return 0;
				}
				case WM_MOUSEWHEEL:
				{
					c->MouseWheel(ww, ll);
					return 0;
				}
				case WM_LBUTTONDOWN:
				{
					c->LeftDown(ww, ll);
					return 0;
				}
				case WM_RBUTTONDOWN:
				{
					c->RightDown(ww, ll);
					return 0;
				}
				case WM_LBUTTONUP:
				{
					c->LeftUp(ww, ll);
					return 0;
				}
				case WM_LBUTTONDBLCLK:
				{
					c->LeftDoubleClick(ww, ll);
					return 0;
				}
				case WM_ERASEBKGND:
				{
					return 1;
				}
				case WM_PAINT:
				{
					PAINTSTRUCT ps;
					BeginPaint(hh, &ps);

					RECT rc;
					GetClientRect(hh, &rc);
					if (!z->fa)
						D2D1CreateFactory(D2D1_FACTORY_TYPE::D2D1_FACTORY_TYPE_MULTI_THREADED, &z->fa);
					if (!z->d)
					{
						//				D2D1_RENDER_TARGET_PROPERTIES p;
						D2D1_HWND_RENDER_TARGET_PROPERTIES hp;
						hp.hwnd = hh;
						hp.pixelSize.width = rc.right;
						hp.pixelSize.height = rc.bottom;
						z->d.Release();

						z->fa->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hh, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)), &z->d);
					}
					z->d->BeginDraw();
					z->c->PaintWindow = hh;
					z->c->Paint(z->fa, z->d, rc);
					[[maybe_unused]] auto hr = z->d->EndDraw();
					EndPaint(hh, &ps);
					return 0;
				}

				case WM_SIZE:
				{
					if (!z->d)
						return 0;

					RECT rc;
					GetClientRect(hh, &rc);
					D2D1_SIZE_U u;
					u.width = rc.right;
					u.height = rc.bottom;
					z->d->Resize(u);
					return 0;
				}

			}
			return 0;
		};
		Z z;
		z.c = this;
		z.SR = SR;
		DialogBoxIndirectParam(0, (LPCDLGTEMPLATE)res, hh, dp, (LPARAM)&z);
		DestroyBrushes();
	}


	void PaintThresholdLines(ID2D1RenderTarget* r)
	{
		if (true)
		{
			// Threshold lines
			for (float dbs : { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -24.0f, -48.0f })
			{
				float V = (float)dB2V((float)dbs);
				float y = (float)V2Y(V);

				D2D1_POINT_2F p1, p2;

				p1.y = y;
				p1.x = rc.left;
				p2.x = rc.right;
				p2.y = y;

				wchar_t t[100] = { 0 };
				swprintf_s(t, L"%.f", dbs);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
#ifdef TURBO_PLAY
				auto rrs = AE::MeasureString(WriteFactory, Text, t, (UINT32)wcslen(t));
#else
				std::tuple<float, float> rrs = std::make_tuple(30.0f, 30.0f);
#endif

				D2D1_RECT_F ly = {};
				ly.left = p1.x + 2;
				ly.top = p1.y - 25;
				ly.bottom = ly.top + 50;
				p1.x += std::get<0>(rrs) + 3;
				ly.right = p1.x + 1;

				D2D1_RECT_F ly2 = {};
				ly2.left = p2.x - std::get<0>(rrs);
				ly2.right = p2.x;
				ly2.top = p1.y - 25;
				ly2.bottom = ly.top + 50;
				p2.x -= std::get<0>(rrs) + 3;


				if (dbs == 0)
					r->DrawLine(p1, p2, SelectBrush);
				else
					r->DrawLine(p1, p2, GrayBrush);

				r->DrawTextW(t, (UINT32)wcslen(t), Text, ly, WhiteBrush);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, ly2, WhiteBrush);

			}
		}

	}

	// https://www.geeksforgeeks.org/program-for-point-of-intersection-of-two-lines/
	D2D1_POINT_2F lineLineIntersection(D2D1_POINT_2F A, D2D1_POINT_2F B, D2D1_POINT_2F C, D2D1_POINT_2F D)
	{
		// Line AB represented as a1x + b1y = c1 
		double a1 = B.y - A.y;
		double b1 = A.x - B.x;
		double c1 = a1 * (A.x) + b1 * (A.y);

		// Line CD represented as a2x + b2y = c2 
		double a2 = D.y - C.y;
		double b2 = C.x - D.x;
		double c2 = a2 * (C.x) + b2 * (C.y);

		double determinant = a1 * b2 - a2 * b1;

		if (determinant == 0)
		{
			// The lines are parallel. This is simplified 
			// by returning a pair of FLT_MAX 
			return { FLT_MAX, FLT_MAX };
		}
		else
		{
			double x = (b2 * c1 - b1 * c2) / determinant;
			double y = (a1 * c2 - a2 * c1) / determinant;
			return { (float)x, (float)y };
		}
	}

	std::wstring GetName()
	{
		wchar_t t[1000] = { 0 };
		if (b.empty())
			swprintf_s(t, 1000, L"Compressor");
		if (Single())
		{
			if (b[0].comp.IsExpander())
				swprintf_s(t, 1000, L"Expander %.1f dB 1:%.1f", b[0].comp.threshold, b[0].comp.ratio);
			else
				swprintf_s(t, 1000, L"Compressor %.1f dB %.1f:1", b[0].comp.threshold, b[0].comp.ratio);
		}
		else
			swprintf_s(t, 1000, L"Multiband Dynamics");
		bool AnyChain = 0;
		for (auto& ch : Chains)
		{
			if (ch.A)
			{
				AnyChain = true;
				break;
			}
		}
		if (AnyChain)
			wcscat_s(t, 1000, L" - SC");
		return t;
	}

	QuickFFT2<float> f4;
	std::vector<D2D1_POINT_2F> ptsX;

	inline void FillPolygon(ID2D1Factory* f, ID2D1RenderTarget* r, D2D1_POINT_2F* p, size_t num, ID2D1Brush* b, FLOAT szx, bool Close)
	{
		// Convert POINT to D2D1_POINT_2F
		if (!p || !num)
			return;


		CComPtr<ID2D1PathGeometry> pg = 0;
		CComPtr<ID2D1GeometrySink> pgs = 0;
		f->CreatePathGeometry(&pg);
		if (pg)
		{
			pg->Open(&pgs);
			if (pgs)
			{
				D2D1_POINT_2F fb;
				fb.x = (FLOAT)p[0].x;
				fb.y = (FLOAT)p[0].y;
				// Use D2D1_FIGURE_BEGIN_FILLED for filled
				D2D1_FIGURE_BEGIN fg = D2D1_FIGURE_BEGIN_HOLLOW;
				if (szx == 0)
					fg = D2D1_FIGURE_BEGIN_FILLED;
				D2D1_FIGURE_END fe;
				if (Close)
					fe = D2D1_FIGURE_END_CLOSED;
				else
					fe = D2D1_FIGURE_END_OPEN;
				pgs->BeginFigure(fb, fg);
				for (size_t i = 1; i < num; i++)
				{
					D2D1_POINT_2F& a = p[i];
					if (&a == &p[0])
						continue;
					D2D1_POINT_2F fu;
					fu.x = a.x;
					fu.y = a.y;
					pgs->AddLine(fu);
				}
				pgs->EndFigure(fe);
				pgs->Close();
			}
			if (szx > 0)
				r->DrawGeometry(pg, b, szx);
			else
				r->FillGeometry(pg, b);
		}
	}

	inline void DrawWave(ID2D1Factory* f, ID2D1RenderTarget* r, D2D1_RECT_F& rc, ID2D1SolidColorBrush* bg, ID2D1SolidColorBrush* fg, ID2D1SolidColorBrush* redw, float* smp, int ns, int Mode)
	{
		if (ns == 0 || smp == 0)
			return;

		if (Mode == 1)
		{
			while (ns > 0 && (ns & (ns - 1)) != 0)
			{
				ns--;
			}
			while (ns > 4096)
				ns /= 2;
		}

		if (Mode == 1)
		{
			f4.Prepare(smp, ns);
			smp = f4.Transform();
			ns /= 2; // take only half part
		}

		if (bg)
			r->FillRectangle(rc, bg);

		if (Mode == 0)
		{
			ptsX.clear();
			bool R = false;
			float MaxA = 0;
			auto mw = rc.right - rc.left;
			if (mw == 0)
				return;
			int step = (int)(ns / mw);
			for (int i = 0; i < ns; i += step)
			{
				D2D1_POINT_2F pp;

				// In ns, rc.right
				// in i,   ?
				pp.x = ((rc.right - rc.left) * i) / (float)ns;
				pp.x += rc.left;

				float s = smp[i];
				auto fs = fabs(s);
				if (fs > MaxA)
					MaxA = fs;
				if (MaxA > 1.0f)
				{

				}
				if (Mode == 0)
					s += 1.0f;
				s /= 2.0f;

				// In rc.bottom, 1.0f
				// ?             s
				pp.y = (rc.bottom - rc.top) * s;
				pp.y += rc.top;
				if (pp.y > rc.bottom)
				{
					pp.y = rc.bottom;
					R = true;
				}
				if (pp.y < rc.top)
				{
					pp.y = rc.top;
					R = true;
				}
				ptsX.push_back(pp);
			}
			FillPolygon(f, r, ptsX.data(), ptsX.size(), R ? redw : fg, 1, 0);
		}
		if (Mode == 1)
		{
			int Bars = 16;
			int SamplesPerBar = ns / Bars;
			float WidthPerBar = (rc.right - rc.left) / (float)Bars;
			int e = 0;
			for (int i = 0; i < Bars; i++)
			{
				D2D1_RECT_F rr = { 0 };
				rr.left = rc.left + i * WidthPerBar + 1;
				rr.right = rr.left + (WidthPerBar - 2);
				rr.top = rc.top;
				rr.bottom = rc.bottom;
				float S = 0;
				for (int h = 0; h < SamplesPerBar; h++)
				{
					if (e >= ns)
						break;
					float s = sqrt(smp[e] * smp[e] + smp[e + ns] * smp[e + ns]);
					e++;

					s /= (float)(ns * 2);

					s *= 120.0f;
					s = fabs(s);
					S += s;
				}
				S /= (float)(SamplesPerBar);
				rr.top = rc.top + (rc.bottom - rc.top) * (1.0f - S);
				r->FillRectangle(rr, fg);
			}
		}

	}

	bool ChangingDataSize = 0;
	int DataSize = 100;
	virtual void Paint(ID2D1Factory* fact, ID2D1RenderTarget* r, RECT rrc)
	{
		r->Clear();
		rc = FromR(rrc);
		auto rfull = rc;
		if (ShowDataMode > 0)
			rc.bottom -= DataSize;
		std::lock_guard<std::recursive_mutex> lg(mu);
		wchar_t t[1000] = { 0 };
		CreateBrushes(r);
		auto rr = rc;
		r->FillRectangle(rc, BGBrush);

		PaintThresholdLines(r);

		bool M = false;
		if (!Single())
			M = true;

		for (auto& bb : b)
		{
			// Verticals
			if (M)
			{
				float vertX = Freq2X(bb.to * MaxHz);
				r->DrawLine({ vertX,rc.top + 5 }, { vertX,rc.bottom }, WhiteBrush, 2);
			}

			// Threshold
			float FromHz = bb.from * MaxHz;
			float ToHz = bb.to * MaxHz;

			float V = (float)dB2V((float)bb.comp.threshold);
			float y = (float)V2Y(V);

			D2D1_POINT_2F p1, p2;

			p1.y = y;
			p1.x = rc.left;
			p2.x = rc.right;
			p2.y = y;

			p1.x = Freq2X(FromHz);
			p2.x = Freq2X(ToHz);

			D2D1_RECT_F ly = {};
			ly.left = p1.x + 2;
			ly.right = p2.x - 5;
			ly.top = rc.top + 10;
			ly.bottom = ly.top + 50;

			if (M)
			{
				wcscpy_s(t, 1000, HZString(FromHz, ToHz).c_str());
				if (bb.comp.Mode == COMPMODE::COMPRESSOR)
					wcscat_s(t, 1000, L" CMP");
				else
				if (bb.comp.Mode == COMPMODE::GATE)
					wcscat_s(t, 1000, L" GT");
				else
					wcscat_s(t, 1000, L" EXP");
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, ly, WhiteBrush);
			}

			swprintf_s(t, 100, L"%.1f dB", bb.comp.threshold);
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
#ifdef TURBO_PLAY
			auto rrs = AE::MeasureString(WriteFactory, Text, t, (UINT32)wcslen(t));
#else
			std::tuple<float, float> rrs = std::make_tuple(30.0f, 30.0f);
#endif

			ly.top = p1.y - 40;
			ly.bottom = p1.y - 5;

			r->DrawLine(p1, p2, YellowBrush, 3);
			bb.ThresholdRect = { p1.x,p1.y - 3,p2.x,p2.y + 3 };
			r->DrawTextW(t, (UINT32)wcslen(t), Text, ly, WhiteBrush);

			if (bb.comp.Mode == COMPMODE::GATE)
			{
				// Hysteresis
				auto ly3 = ly;
				ly3.left = p1.x + 2;
				ly3.right = p2.x - 5;
				float Vh = (float)dB2V((float)bb.comp.hysteresis);
				float yh = (float)V2Y(Vh);

				ly3.top = yh;
				ly3.bottom = ly3.top + 10;

				swprintf_s(t, 100, L"%.1f dB", bb.comp.hysteresis);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
#ifdef TURBO_PLAY
				auto rrs5 = AE::MeasureString(WriteFactory, Text, t, (UINT32)wcslen(t));
#else
				std::tuple<float, float> rrs5 = std::make_tuple(30.0f, 30.0f);
#endif
				r->DrawLine({ ly3.left,ly3.top }, { ly3.right,ly3.top }, GrayBrush, 3);
				bb.HysteresisRect = ly3;
				r->DrawTextW(t, (UINT32)wcslen(t), Text, ly3, WhiteBrush);
			}

			if (bb.comp.Mode != COMPMODE::GATE)
			{
				// Ratio line 
				D2D1_POINT_2F p1a, p2a;
				p1a = { p1.x,rc.bottom };
				if (bb.comp.IsExpander())
					p1a.y = rc.top + 5;
				p2a = { p2.x - 2,rc.top + 5 };
				if (bb.comp.IsExpander())
					p2a.y = rc.bottom;

				auto pb = lineLineIntersection(p1, p2, p1a, p2a);
				if (bb.comp.Mode == COMPMODE::UEXPANDER)
				{
					p1a.y = rc.bottom;
				}

				if (bb.comp.Mode != COMPMODE::UDEXPANDER)
					r->DrawLine(p1a, pb, WhiteBrush, 3);
				bb.RatioPoints1 = { p1a.x,p1a.y,pb.x,pb.y };
				float dbn = bb.comp.threshold + (-bb.comp.threshold / bb.comp.ratio);
				if (bb.comp.Mode == COMPMODE::DEXPANDER)
					dbn = bb.comp.threshold * bb.comp.ratio;
				if (bb.comp.Mode == COMPMODE::UEXPANDER)
					dbn = bb.comp.threshold * (1.0f/bb.comp.ratio);
				if (bb.comp.Mode == COMPMODE::UDEXPANDER)
					dbn = bb.comp.threshold * (1.0f / bb.comp.ratio);
				p2a.y = V2Y(dB2V(dbn));
				r->DrawLine(pb, p2a, SelectBrush, 3);
				if (bb.comp.Mode == COMPMODE::UDEXPANDER)
				{
					auto p2aa = p2a;
					p2aa.y = V2Y(dB2V(bb.comp.threshold * bb.comp.ratio));
					r->DrawLine(pb, p2aa, SelectBrush, 1);
				}

				bb.RatioPoints2 = { pb.x,pb.y,p2a.x,p2a.y };
				D2D1_RECT_F ly2 = {};
				ly2.left = pb.x - 15;
				ly2.right = pb.x + 15;
				ly2.top = pb.y - 30;
				ly2.bottom = ly2.top + 30;
				r->DrawLine({ pb.x,ly.top }, { pb.x,ly2.bottom - 5 }, WhiteBrush);

				ly2.top -= 30;
				if (bb.comp.IsExpander())
					swprintf_s(t, 100, L"1:%.1f", bb.comp.ratio);
				else
					swprintf_s(t, 100, L"%.1f:1", bb.comp.ratio);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, ly2, WhiteBrush);
			}

			bb.BandRect = rc;
			bb.BandRect.left = p1.x;
			bb.BandRect.right = p2.x;

			// Thres button
			bb.ButtThres.left = p1.x + 25;
			bb.ButtThres.right = p2.x - 5;
			if ((bb.ButtThres.right - bb.ButtThres.left) > 100)
				bb.ButtThres.right = bb.ButtThres.left + 100;
			bb.ButtThres.top = rc.top + 40;
			bb.ButtThres.bottom = bb.ButtThres.top + 20;
			D2D1_ROUNDED_RECT r4 = { bb.ButtThres,2,2 };
			r->DrawRoundedRectangle(r4, WhiteBrush);
			swprintf_s(t, 100, L"Threshold %.1f dB", bb.comp.threshold);
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtThres, WhiteBrush);

			if (bb.comp.Mode == COMPMODE::GATE)
			{
				// Ratio button
				bb.ButtRatioHyst = bb.ButtThres;
				bb.ButtRatioHyst.top = bb.ButtThres.bottom + 5;
				bb.ButtRatioHyst.bottom = bb.ButtRatioHyst.top + 20;
				r4 = { bb.ButtRatioHyst,2,2 };
				r->DrawRoundedRectangle(r4, WhiteBrush);
				swprintf_s(t, 100, L"Hysteresis %.1f dB", bb.comp.hysteresis);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtRatioHyst, WhiteBrush);
			}

			if (bb.comp.Mode != COMPMODE::GATE)
			{
				// Ratio button
				bb.ButtRatioHyst = bb.ButtThres;
				bb.ButtRatioHyst.top = bb.ButtThres.bottom + 5;
				bb.ButtRatioHyst.bottom = bb.ButtRatioHyst.top + 20;
				r4 = { bb.ButtRatioHyst,2,2 };
				r->DrawRoundedRectangle(r4, WhiteBrush);
				if (bb.comp.IsExpander())
					swprintf_s(t, 100, L"Ratio 1:%.1f", bb.comp.ratio);
				else
					swprintf_s(t, 100, L"Ratio %.1f:1", bb.comp.ratio);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtRatioHyst, WhiteBrush);
			}

			// Attack button
			bb.ButtAttack = bb.ButtRatioHyst;
			bb.ButtAttack.top = bb.ButtRatioHyst.bottom + 10;
			bb.ButtAttack.bottom = bb.ButtAttack.top + 20;
			r4 = { bb.ButtAttack,2,2 };
			r->DrawRoundedRectangle(r4, YellowBrush);
			swprintf_s(t, 100, L"Attack %i ms", (int)(bb.comp.attack * 1000.0f));
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtAttack, WhiteBrush);

			// Release button
			bb.ButtRelease = bb.ButtAttack;
			bb.ButtRelease.top = bb.ButtAttack.bottom + 5;
			bb.ButtRelease.bottom = bb.ButtRelease.top + 20;
			r4 = { bb.ButtRelease,2,2 };
			r->DrawRoundedRectangle(r4, YellowBrush);
			swprintf_s(t, 100, L"Release %i ms", (int)(bb.comp.release * 1000.0f));
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtRelease, WhiteBrush);

			// Hold button
			bb.ButtHold = bb.ButtRelease;
			bb.ButtHold.top = bb.ButtRelease.bottom + 5;
			bb.ButtHold.bottom = bb.ButtHold.top + 20;
			r4 = { bb.ButtHold,2,2 };
			r->DrawRoundedRectangle(r4, YellowBrush);
			swprintf_s(t, 100, L"Hold %i ms", (int)(bb.comp.hold * 1000.0f));
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtHold, WhiteBrush);


			// Makeup button
			if (bb.comp.Mode != COMPMODE::GATE)
			{
				bb.ButtMakeup = bb.ButtHold;
				bb.ButtMakeup.top = bb.ButtHold.bottom + 10;
				bb.ButtMakeup.bottom = bb.ButtMakeup.top + 20;
				r4 = { bb.ButtMakeup,2,2 };
				r->DrawRoundedRectangle(r4, SelectBrush);
				swprintf_s(t, 100, L"Makeup %.1f dB", bb.comp.makeup);
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, bb.ButtMakeup, WhiteBrush);
			}

		}

		// Chains threshold
		for (auto& ch : Chains)
		{
			if (ch.A == false)
				continue;
			// Paint the threshold
			D2D1_RECT_F ly3 = {};
			ly3.left = rc.left + 2;
			ly3.right = rc.right - 5;
			float Vh = (float)dB2V((float)ch.thr);
			float yh = (float)V2Y(Vh);

			ly3.top = yh;
			ly3.bottom = ly3.top + 10;
			swprintf_s(t, 1000, L"%s %.1f dB", ch.n.c_str(),ch.thr);
			Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
#ifdef TURBO_PLAY
			auto rrs5 = AE::MeasureString(WriteFactory, Text, t, (UINT32)wcslen(t));
#else
			std::tuple<float, float> rrs5 = std::make_tuple(30.0f, 30.0f);
#endif
			r->DrawLine({ ly3.left,ly3.top }, { ly3.right,ly3.top }, GrayBrush, 3);
			ch.thres_rect = ly3;
			ly3.top += 5;
			r->DrawTextW(t, (UINT32)wcslen(t), Text, ly3, WhiteBrush);

		}

		if (!Chains.empty())
		{
			D2D1_ROUNDED_RECT r4 = { {rc.right - 100,rc.bottom - 50,rc.right - 30,rc.bottom - 25},2,2 };
			bool AnyChain = 0;
			ChainRect = r4.rect;
			std::wstring cht;
			for (auto& ch : Chains)
			{
				if (ch.A)
				{
					AnyChain = true;
					if (cht.empty())
						cht = ch.n;
					else
						cht = L"Multiple";
				}
			}
			if (AnyChain)
			{
				r->FillRoundedRectangle(r4, SelectBrush);
				swprintf_s(t, 1000, cht.c_str());
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, r4.rect, BlackBrush);
			}
			else
			{
				r->FillRoundedRectangle(r4, GrayBrush);
				swprintf_s(t, 100, L"Side Chain");
				Text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				Text->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				r->DrawTextW(t, (UINT32)wcslen(t), Text, r4.rect, WhiteBrush);
			}
		}
		// Draw compression
		for (auto& bb : b)
		{
			D2D1_RECT_F re = { bb.BandRect.left + 30,0,bb.BandRect.left + 20,bb.BandRect.bottom - 1 };
			re.top = V2Y(dB2V(bb.LastInputDB));
			auto br = GrayBrush;
			auto col = br->GetColor();
			if (bb.comp.state == COMPSTATE::NONE)
				br->SetColor({ 0.7f,0.7f,0.7f,0.4f });
			if (bb.comp.state == COMPSTATE::ATTACK)
				br->SetColor({ 0.7f,0.7f,0.0f,0.4f });
			if (bb.comp.state == COMPSTATE::ACTION)
				br->SetColor({ 0.9f,0.0f,0.0f,0.4f });
			if (bb.comp.state == COMPSTATE::HOLD)
				br->SetColor({ 0.0f,0.9f,0.0f,0.4f });
			if (bb.comp.state == COMPSTATE::RELEASE)
				br->SetColor({ 0.0f,0.7f,0.0f,0.4f });
			r->FillRectangle(re, br);
			br->SetColor(col);
		}

		// Paint the wave
		if (dins.size() > 0 && douts.size() > 0 && ShowDataMode > 0)
		{
			D2D1_RECT_F rc2 = rfull;
			rc2.top = rc2.bottom - DataSize;
			rc2.bottom = rc2.top + DataSize/2.0f;
			D2D1_RECT_F rc2a = rfull;
			rc2a.top = rc2a.bottom - DataSize / 2;
			CComPtr<ID2D1Factory> fat;
			r->GetFactory(&fat);

			for (size_t i = 0; i < dins.size() && i < douts.size(); i++)
			{
				auto& din = dins[i];
				auto& dout = douts[i];

				auto rcx = rc2;
				float he = rc2.bottom - rc2.top;
				he /= dins.size();
				rcx.top = rc2.top + (i * he);
				rcx.bottom = rcx.top + he;
				DrawWave(fat, r, rcx, 0, YellowBrush, YellowBrush, din.data(), (int)din.size(), ShowDataMode - 1);

				rcx = rc2a;
				he = rc2a.bottom - rc2a.top;
				he /= douts.size();
				rcx.top = rc2a.top + (i * he);
				rcx.bottom = rcx.top + he;
				DrawWave(fat, r, rcx, 0, SelectBrush, SelectBrush, dout.data(), (int)dout.size(), ShowDataMode - 1);
			}
		}

	}

};

inline void MMCB::Dirty(COMP* q, bool)
{
	if (!q)
		return;
	q->Build(q->TheSR, 0,1);
}


inline void MMCB::RedrawRequest(COMP* p)
{
	if (!IsWindow(hC))
		return;
	InvalidateRect(hC, 0, TRUE);
	UpdateWindow(hC);
}





