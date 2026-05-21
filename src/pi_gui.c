// pi_gui.c — Win32 GUI for Pi Calculator (background thread + file output)

#ifndef UNICODE
#define UNICODE
#endif
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>

#define ID_INPUT   101
#define ID_CALC    102
#define ID_RESULT  103
#define ID_LIVE    104
#define ID_STATUS  105
#define ID_FILEPATH 106
#define WM_NEW_DIGITS (WM_APP + 1)
#define WM_LIVE_DONE  (WM_APP + 2)

static void bs_inner(int a,int b,mpz_t p,mpz_t q,mpz_t t){
    if(b-a==1){mpz_set_si(p,-24);mpz_mul_si(p,p,6L*a-5);mpz_mul_si(p,p,2L*a-1);mpz_mul_si(p,p,6L*a-1);
    mpz_set_ui(q,(unsigned long)a);mpz_mul_ui(q,q,(unsigned long)a);mpz_mul_ui(q,q,(unsigned long)a);
    mpz_mul_ui(q,q,640320U);mpz_mul_ui(q,q,640320U);mpz_mul_ui(q,q,640320U);
    mpz_set_si(t,545140134L);mpz_mul_si(t,t,a);mpz_add_ui(t,t,13591409);mpz_mul(t,t,p);return;}
    int m=(a+b)/2;mpz_t p1,q1,t1,p2,q2,t2;mpz_inits(p1,q1,t1,p2,q2,t2,NULL);
    bs_inner(a,m,p1,q1,t1);bs_inner(m,b,p2,q2,t2);
    mpz_mul(p,p1,p2);mpz_mul(q,q1,q2);mpz_mul(t,t2,p1);mpz_addmul(t,t1,q2);
    mpz_clears(p1,q1,t1,p2,q2,t2,NULL);
}
typedef struct{mpz_t P,Q,T;int n;}BSCache;
static void ci(BSCache*c){mpz_inits(c->P,c->Q,c->T,NULL);c->n=0;}
static void cc(BSCache*c){mpz_clears(c->P,c->Q,c->T,NULL);}
static void ce(BSCache*c,int tgt){
    if(tgt<=c->n)return;
    if(c->n==0){mpz_t pu;mpz_init(pu);bs_inner(1,tgt,pu,c->Q,c->T);mpz_set(c->P,pu);mpz_clear(pu);c->n=tgt;return;}
    mpz_t pn,qn,tn,pm,qm,tm;mpz_inits(pn,qn,tn,pm,qm,tm,NULL);
    bs_inner(c->n,tgt,pn,qn,tn);mpz_mul(pm,c->P,pn);mpz_mul(qm,c->Q,qn);
    mpz_mul(tm,tn,c->P);mpz_addmul(tm,c->T,qn);
    mpz_swap(c->P,pm);mpz_swap(c->Q,qm);mpz_swap(c->T,tm);c->n=tgt;
    mpz_clears(pn,qn,tn,pm,qm,tm,NULL);
}
static char*pi_str(BSCache*c,int prec){
    int N=prec,s=5;mpz_t rd,sq,num,den,ps;mpz_inits(rd,sq,num,den,ps,NULL);
    mpz_ui_pow_ui(rd,10,2u*(unsigned)(N+s));mpz_mul_ui(rd,rd,10005);mpz_sqrt(sq,rd);mpz_clear(rd);
    mpz_mul(num,sq,c->Q);mpz_mul_ui(num,num,426880);
    mpz_mul_ui(den,c->Q,13591409);mpz_add(den,den,c->T);mpz_tdiv_q(ps,num,den);
    mpz_clears(sq,num,den,NULL);
    char*f=mpz_get_str(NULL,10,ps);mpz_clear(ps);
    int fl=(int)strlen(f);char*r=(char*)malloc((size_t)N+3);
    if(!r){free(f);return NULL;}
    r[0]=f[0];r[1]='.';int av=fl-1;
    if(av>=N)memcpy(r+2,f+1,(size_t)N);
    else{memcpy(r+2,f+1,(size_t)av);memset(r+2+av,'0',(size_t)(N-av));}
    r[N+2]=0;free(f);return r;
}

typedef struct{
    HWND hwnd;int start_terms;volatile int*stop_flag;
    char out_file[260];
}ThreadParam;

static unsigned __stdcall live_worker(void*arg){
    ThreadParam*tp=(ThreadParam*)arg;
    BSCache c;ci(&c);
    char*prev=strdup("3.");
    int n_terms=tp->start_terms;
    LARGE_INTEGER t0,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&t0);
    int total=0;
    FILE*fout=NULL;
    if(tp->out_file[0]){fout=fopen(tp->out_file,"w");if(fout)fprintf(fout,"3.");}

    while(!*(tp->stop_flag)){
        ce(&c,n_terms);
        int cp=(n_terms-3)*14;if(cp<50)cp=50;
        char*r=pi_str(&c,cp);
        if(!r)break;
        int pl=(int)strlen(prev),cl=(int)strlen(r);
        char*new_part=NULL;
        if(cl>pl){
            new_part=strdup(r+pl);total=cl-2;
            if(fout){fprintf(fout,"%s",new_part);fflush(fout);}
        }
        free(prev);prev=r;
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        double el=(double)(now.QuadPart-t0.QuadPart)/freq.QuadPart;
        if(new_part){
            char*msg=(char*)malloc(256+strlen(new_part)+1);
            sprintf(msg,"%s|%d|%.3f",new_part,total,el);
            free(new_part);
            PostMessageW(tp->hwnd,WM_NEW_DIGITS,(WPARAM)msg,0);
        }
        n_terms=(int)((double)n_terms*1.3+1);
    }
    cc(&c);free(prev);
    if(fout)fclose(fout);
    PostMessageW(tp->hwnd,WM_LIVE_DONE,(WPARAM)(tp->out_file[0]?1:0),0);
    return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    static HWND hInput,hCalc,hResult,hLive,hStatus,hFile;
    static HFONT hFont;
    static volatile int stop_live=0;
    static HANDLE hThread=NULL;

    switch(msg){
    case WM_CREATE:{
        HINSTANCE hi=((LPCREATESTRUCT)lp)->hInstance;
        hFont=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,FIXED_PITCH|FF_MODERN,L"Consolas");
        CreateWindowW(L"STATIC",L"Digits:",WS_CHILD|WS_VISIBLE,10,10,45,24,hwnd,NULL,hi,NULL);
        hInput=CreateWindowW(L"EDIT",L"1000",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,55,10,70,24,hwnd,(HMENU)ID_INPUT,hi,NULL);
        SendMessageW(hInput,WM_SETFONT,(WPARAM)hFont,TRUE);
        hCalc=CreateWindowW(L"BUTTON",L"Calc",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,130,9,50,26,hwnd,(HMENU)ID_CALC,hi,NULL);
        SendMessageW(hCalc,WM_SETFONT,(WPARAM)hFont,TRUE);
        hLive=CreateWindowW(L"BUTTON",L"Live",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,185,9,50,26,hwnd,(HMENU)ID_LIVE,hi,NULL);
        SendMessageW(hLive,WM_SETFONT,(WPARAM)hFont,TRUE);
        CreateWindowW(L"STATIC",L"File:",WS_CHILD|WS_VISIBLE,245,12,30,20,hwnd,NULL,hi,NULL);
        hFile=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER,275,10,205,24,hwnd,(HMENU)ID_FILEPATH,hi,NULL);
        SendMessageW(hFile,WM_SETFONT,(WPARAM)hFont,TRUE);
        hResult=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",
            WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL|WS_HSCROLL|WS_VSCROLL,
            10,45,470,180,hwnd,(HMENU)ID_RESULT,hi,NULL);
        SendMessageW(hResult,WM_SETFONT,(WPARAM)hFont,TRUE);
        hStatus=CreateWindowW(L"STATIC",L"Ready",WS_CHILD|WS_VISIBLE,10,235,470,20,hwnd,(HMENU)ID_STATUS,hi,NULL);
        SendMessageW(hStatus,WM_SETFONT,(WPARAM)hFont,TRUE);
        break;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_CALC&&!hThread){
            wchar_t bf[32];GetWindowTextW(hInput,bf,32);int pr=_wtoi(bf);if(pr<1)break;
            SetWindowTextW(hStatus,L"Computing...");SetWindowTextW(hResult,L"");
            EnableWindow(hCalc,FALSE);EnableWindow(hLive,FALSE);
            LARGE_INTEGER t1,t2,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&t1);
            BSCache c;ci(&c);ce(&c,pr/14+3);char*r=pi_str(&c,pr);cc(&c);
            QueryPerformanceCounter(&t2);double ms=(double)(t2.QuadPart-t1.QuadPart)*1000.0/freq.QuadPart;
            EnableWindow(hCalc,TRUE);EnableWindow(hLive,TRUE);
            if(!r){SetWindowTextW(hStatus,L"OOM!");break;}
            int wl=MultiByteToWideChar(CP_UTF8,0,r,-1,NULL,0);
            wchar_t*wr=(wchar_t*)malloc((size_t)wl*sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8,0,r,-1,wr,wl);SetWindowTextW(hResult,wr);free(wr);free(r);
            wchar_t st[128];swprintf(st,128,L"%d digits in %.1f ms",pr,ms);
            SetWindowTextW(hStatus,st);
        }
        else if(LOWORD(wp)==ID_LIVE){
            if(hThread){
                stop_live=1;SetWindowTextW(hLive,L"Stop...");EnableWindow(hCalc,FALSE);
            }else{
                wchar_t bf[32];GetWindowTextW(hInput,bf,32);int sp=_wtoi(bf);if(sp<100)sp=100;
                wchar_t fpath[260];GetWindowTextW(hFile,fpath,260);
                stop_live=0;SetWindowTextW(hLive,L"Stop");EnableWindow(hCalc,FALSE);
                SetWindowTextW(hResult,L"3.");SetWindowTextW(hStatus,L"Running...");
                ThreadParam*tp=(ThreadParam*)malloc(sizeof(ThreadParam));
                tp->hwnd=hwnd;tp->start_terms=sp/14+3;if(tp->start_terms<15)tp->start_terms=15;
                tp->stop_flag=&stop_live;tp->out_file[0]=0;
                if(fpath[0]){WideCharToMultiByte(CP_UTF8,0,fpath,-1,tp->out_file,260,NULL,NULL);}
                hThread=(HANDLE)_beginthreadex(NULL,0,live_worker,tp,0,NULL);
            }
        }
        break;
    case WM_NEW_DIGITS:{
        char*msg=(char*)wp;if(!msg)break;
        char*new_part=msg;char*sep1=strchr(msg,'|');
        if(!sep1){free(msg);break;}*sep1=0;
        int td=atoi(sep1+1);char*sep2=strchr(sep1+1,'|');
        if(sep2){*sep2=0;
            int wl=MultiByteToWideChar(CP_UTF8,0,new_part,-1,NULL,0);
            wchar_t*wn=(wchar_t*)malloc((size_t)wl*sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8,0,new_part,-1,wn,wl);
            int cl=GetWindowTextLengthW(hResult);
            SendMessageW(hResult,EM_SETSEL,(WPARAM)cl,(LPARAM)cl);
            SendMessageW(hResult,EM_REPLACESEL,FALSE,(LPARAM)wn);free(wn);
            double el=atof(sep2+1);wchar_t st[128];
            swprintf(st,128,L"%d digits, %.1f s",td,el);
            SetWindowTextW(hStatus,st);SendMessageW(hResult,EM_SCROLLCARET,0,0);
        }free(msg);break;
    }
    case WM_LIVE_DONE:{
        if(hThread){CloseHandle(hThread);hThread=NULL;}
        stop_live=0;SetWindowTextW(hLive,L"Live");EnableWindow(hCalc,TRUE);
        if(wp){wchar_t st[128];swprintf(st,128,L"Done. Saved to output file");SetWindowTextW(hStatus,st);}
        break;
    }
    case WM_SIZE:{
        int w=LOWORD(lp),h=HIWORD(lp);
        SetWindowPos(hResult,NULL,10,45,w-20,h-85,SWP_NOZORDER);
        SetWindowPos(hStatus,NULL,10,h-30,w-20,20,SWP_NOZORDER);
        break;
    }
    case WM_DESTROY:
        stop_live=1;if(hThread){WaitForSingleObject(hThread,2000);CloseHandle(hThread);}
        DeleteObject(hFont);PostQuitMessage(0);break;
    default:return DefWindowProcW(hwnd,msg,wp,lp);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int ns){
    InitCommonControls();
    WNDCLASSW wc={0};wc.lpfnWndProc=WndProc;wc.hInstance=hi;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName=L"PiCalcClass";
    if(!RegisterClassW(&wc))return 1;
    RECT rc={0,0,490,310};AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
    HWND hwnd=CreateWindowW(L"PiCalcClass",L"Pi Calculator",
        WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME,
        CW_USEDEFAULT,CW_USEDEFAULT,rc.right-rc.left,rc.bottom-rc.top,NULL,NULL,hi,NULL);
    if(!hwnd)return 1;
    ShowWindow(hwnd,ns);UpdateWindow(hwnd);
    MSG m;while(GetMessageW(&m,NULL,0,0)){TranslateMessage(&m);DispatchMessageW(&m);}
    return (int)m.wParam;
}
