"""Only opacity policy, cache migration and directly affected render routes."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
import test_library_state as cache_tests

ROOT=Path(__file__).resolve().parents[1]

class OpacityCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cache_tests.LibraryStateTests.setUpClass.__func__(cls)
    def run_case(self,name):cache_tests.LibraryStateTests.run_case(self,name)
    def test_old_opacity_fields_backup_retry_and_preserve_unrelated_data(self):self.run_case('opacity')
    def test_legacy_migration_keeps_progress_bookmarks_order_and_settings(self):self.run_case('migration')
    def test_corrupt_future_or_partial_cache_cannot_be_overwritten(self):self.run_case('invalid')

class OpaqueWindowTests(unittest.TestCase):
    def test_actual_native_style_policy_all_modes_restart_and_saved_parameters(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text()
        helper='static void ApplyReaderOpaqueWindow('+reader.split('static void ApplyReaderOpaqueWindow(',1)[1].split('static RECT _readingRect',1)[0]
        source=r'''
#include "WindowAppearance.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
using HWND=int;using LONG_PTR=int64_t;
const int GWL_EXSTYLE=-20;
struct Settings{unsigned alpha=0,exstyle=0,fs_exstyle=0;int font=-31;unsigned color=0x123456;};
Settings data,*_header=&data;LONG_PTR native=0;int calls=0;
LONG_PTR GetWindowLongPtr(HWND,int){return native;}
void SetWindowLongPtr(HWND,int,LONG_PTR value){native=value;++calls;}
'''+helper+r'''
int main(){for(unsigned oldAlpha:{0u,1u,100u,128u,255u})for(unsigned style:{0u,0x80000u,0x20u,0x80038u,0x8000138u}){
 for(int mode=0;mode<6;++mode){ // normal/focus/exit focus/fullscreen/borderless/restart
  data.alpha=oldAlpha;data.exstyle=style;data.fs_exstyle=style|0x100u;native=style;
  ApplyReaderOpaqueWindow(1);assert(data.alpha==255&&native==(style&~0x80020u));
  assert(data.exstyle==(style&~0x80020u)&&data.fs_exstyle==((style|0x100u)&~0x80020u));
  assert(data.font==-31&&data.color==0x123456);int previous=calls;ApplyReaderOpaqueWindow(1);assert(calls==previous);
  native=data.fs_exstyle;ApplyReaderOpaqueWindow(1);assert(!(native&0x80020u));
 }
}assert(ReaderOpaqueExStyle(int64_t(0x100080038LL))==0x100000018LL);}
'''
        with tempfile.TemporaryDirectory(prefix='reader-opaque-') as directory:
            cpp=Path(directory)/'opaque.cpp';exe=cpp.with_suffix('');cpp.write_text(source)
            command=[*shlex.split(os.environ.get('CXX','g++')),'-std=c++14','-Wall','-Wextra','-I'+str(ROOT/'Reader'),str(cpp),'-o',str(exe)]
            result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(result.returncode,0,result.stdout)
            result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(result.returncode,0,result.stdout)

    def test_retired_transparency_routes_cannot_restore_window_opacity(self):
        reader=(ROOT/'Reader/Reader.cpp').read_text();cache=(ROOT/'Reader/Cache.cpp').read_text()
        for retired in ('UpdateLayeredWindow','SetLayeredWindowAttributes','GetLayeredWindowAttributes','ResetLayerd','VOID OnDraw(','_WndInfo.bLayered','_textAlpha','CreateAlphaBGColorBitmap'):
            self.assertNotIn(retired,reader)
        self.assertIn('CreateWindowEx(ReaderOpaqueExStyle(_header->exstyle)',reader)
        for line in reader.splitlines():
            if 'SetWindowLongPtr(hWnd, GWL_EXSTYLE,' in line:self.assertIn('ReaderOpaqueExStyle',line)
        self.assertIn('schema < CACHE_SCHEMA_VERSION || normalizedOpacity',cache)
        self.assertIn('ReaderNormalizeOpacity(*get_header())',cache)
        self.assertNotIn('WS_EX_LAYERED',cache)
        wheel=reader.split('bool control=(GET_KEYSTATE_WPARAM',1)[1].split('case WM_ERASEBKGND',1)[0]
        self.assertIn('_ctrlWheel.Feed',wheel);self.assertIn('Retired transparency gesture',wheel)
        header=(ROOT/'Reader/Reader.h').read_text();self.assertIn('extern const BYTE   _textAlpha',header)
        for file in ('Reader_zh-cn.rc','Reader.rc'):
            raw=(ROOT/'Reader'/file).read_bytes();text=raw.decode('utf-16') if raw.startswith(b'\xff\xfe') else raw.decode()
            self.assertNotRegex(text,r'透明度|IDC_\w*(?:ALPHA|TRANSPAREN)')
