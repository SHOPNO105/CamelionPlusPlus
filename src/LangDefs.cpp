// Camelion++ - language definitions
// Table order and lexer names follow Notepad++'s ScintillaEditView::_langNameInfoArray
// so LangType values are identical (plugins rely on this).
// Standard headers used in this file (listed explicitly: newer GCC/libstdc++ no longer include them transitively)
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "LangDefs.h"
#include <cstring>
#include <SciLexer.h>

namespace cam {

int keywordClassFromName(const char* s)
{
    if (!s) return -1;
    static const char* names[] = {"instre1","instre2","type1","type2","type3","type4","type5","type6","type7",
        "substyle1","substyle2","substyle3","substyle4","substyle5","substyle6","substyle7","substyle8"};
    for (int i = 0; i < NB_LANG_INDEX; ++i)
        if (std::strcmp(s, names[i]) == 0) return i;
    if (s[0] >= '0' && s[0] <= '8' && s[1] == '\0') return s[0] - '0';
    return -1;
}

#define CPPFAM "0:0,1:2,2:D,3:1"
#define FOLD "fold=1;fold.compact=0;fold.comment=1"
#define L0 "0:0"
#define L01 "0:0,1:1"
#define L012 "0:0,1:1,2:2"
#define L0123 "0:0,1:1,2:2,3:3"
#define L01234 "0:0,1:1,2:2,3:3,4:4"
#define L012345 "0:0,1:1,2:2,3:3,4:4,5:5"
#define L0_6 "0:0,1:1,2:2,3:3,4:4,5:5,6:6"
#define L0_7 "0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7"
#define L0_8 "0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7,8:8"
#define CPPPROPS FOLD ";fold.cpp.comment.explicit=0;fold.preprocessor=1;lexer.cpp.track.preprocessor=0"

static const std::vector<LangDef> g_langs = {
 {"normal","Normal text","Normal text file",0,"null","","",{}},
 {"php","PHP","PHP Hypertext Preprocessor file",1,"phpscript","HTML","",{}},
 {"c","C","C source file",2,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"cpp","C++","C++ source file",3,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"cs","C#","C# source file",4,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"objc","Objective-C","Objective-C source file",5,"objc","0:0,1:2,2:D,3:1,4:3",CPPPROPS,{}},
 {"java","Java","Java source file",6,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"rc","RC","Windows Resource file",7,"cpp","0:0,1:2,3:1",CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"html","HTML","Hyper Text Markup Language file",8,"hypertext","HTML","",{}},
 {"xml","XML","eXtensible Markup Language file",9,"xml","XML","",{}},
 {"makefile","Makefile","Makefile",10,"makefile","",FOLD,{}},
 {"pascal","Pascal","Pascal source file",11,"pascal",L0,FOLD ";fold.preprocessor=1",{}},
 {"batch","Batch","Batch file",12,"batch",L0,FOLD,{}},
 {"ini","ini","MS ini file",13,"props","",FOLD,{}},
 {"nfo","NFO","MSDOS Style/ASCII Art",14,"null","","",{}},
 {"udf","udf","User Defined language file",15,"null","","",{}},
 {"asp","ASP","Active Server Pages script file",16,"hypertext","HTML","",{}},
 {"sql","SQL","Structured Query Language file",17,"sql","0:0,1:1,4:4",FOLD ";sql.backslash.escapes=0",{}},
 {"vb","Visual Basic","Visual Basic file",18,"vb",L0,FOLD,{}},
 {"javascript","Embedded JS","Embedded JavaScript",19,"cpp",CPPFAM,CPPPROPS,{}},
 {"css","CSS","Cascade Style Sheets File",20,"css","0:0,1:1,4:4,6:6",FOLD,{}},
 {"perl","Perl","Perl source file",21,"perl",L0,FOLD,{}},
 {"python","Python","Python file",22,"python",L01,FOLD ";fold.quotes.python=1;lexer.python.decorator.attributes=1;lexer.python.identifier.attributes=1",{{SCE_P_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"lua","Lua","Lua source File",23,"lua",L0_7,FOLD,{{SCE_LUA_IDENTIFIER,4,LANG_INDEX_SUBSTYLE1}}},
 {"tex","TeX","TeX file",24,"tex","",FOLD,{}},
 {"fortran","Fortran free form","Fortran free form source file",25,"fortran",L012,FOLD,{}},
 {"bash","Shell","Unix script file",26,"bash",L0,FOLD,{{SCE_SH_IDENTIFIER,4,LANG_INDEX_SUBSTYLE1},{SCE_SH_SCALAR,4,LANG_INDEX_SUBSTYLE5}}},
 {"actionscript","ActionScript","Flash ActionScript file",27,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"nsis","NSIS","Nullsoft Scriptable Install System script file",28,"nsis",L0123,FOLD,{}},
 {"tcl","TCL","Tool Command Language file",29,"tcl","0:0,1:2,2:1,3:3,4:4,5:5,6:6,7:7,8:8",FOLD,{}},
 {"lisp","Lisp","List Processing language file",30,"lisp",L01,FOLD,{}},
 {"scheme","Scheme","Scheme file",31,"lisp",L01,FOLD,{}},
 {"asm","Assembly","Assembly language source file",32,"asm",L0_7,FOLD ";fold.asm.syntax.based=1;fold.asm.comment.multiline=1;fold.asm.comment.explicit=1",{}},
 {"diff","Diff","Diff file",33,"diff","",FOLD,{}},
 {"props","Properties file","Properties file",34,"props","",FOLD,{}},
 {"postscript","PostScript","PostScript file",35,"ps",L0123,FOLD,{}},
 {"ruby","Ruby","Ruby file",36,"ruby",L0,FOLD,{}},
 {"smalltalk","Smalltalk","Smalltalk file",37,"smalltalk",L0,FOLD,{}},
 {"vhdl","VHDL","VHSIC Hardware Description Language file",38,"vhdl",L0_6,FOLD,{}},
 {"kix","KiXtart","KiXtart file",39,"kix",L012,FOLD,{}},
 {"autoit","AutoIt","AutoIt",40,"au3",L0_6,FOLD ";fold.preprocessor=1",{}},
 {"caml","CAML","Categorical Abstract Machine Language",41,"caml",L012,FOLD,{}},
 {"ada","Ada","Ada file",42,"ada",L0,FOLD,{}},
 {"verilog","Verilog","Verilog file",43,"verilog",L01,FOLD ";fold.preprocessor=1",{}},
 {"matlab","MATLAB","MATrix LABoratory",44,"matlab",L0,FOLD,{}},
 {"haskell","Haskell","Haskell",45,"haskell",L0,FOLD,{}},
 {"inno","Inno Setup","Inno Setup script",46,"inno",L012345,FOLD,{}},
 {"searchResult","Internal Search","Internal Search",47,"searchResult","","",{}},
 {"cmake","CMake","CMake file",48,"cmake",L012,FOLD,{}},
 {"yaml","YAML","YAML Ain't Markup Language",49,"yaml",L0,FOLD,{}},
 {"cobol","COBOL","COmmon Business Oriented Language",50,"COBOL",L012,FOLD,{}},
 {"gui4cli","Gui4Cli","Gui4Cli file",51,"gui4cli",L01234,FOLD,{}},
 {"d","D","D programming language",52,"d",L0_6,FOLD,{}},
 {"powershell","PowerShell","Windows PowerShell",53,"powershell",L012345,FOLD,{}},
 {"r","R","R programming language",54,"r",L012,FOLD ";lexer.r.escape.sequence=1",{}},
 {"jsp","JSP","JavaServer Pages script file",55,"hypertext","HTML","",{}},
 {"coffeescript","CoffeeScript","CoffeeScript file",56,"coffeescript",L0123,FOLD,{}},
 {"json","json","JSON file",57,"json",L01,"fold=1;fold.compact=0;lexer.json.escape.sequence=1",{}},
 {"javascript.js","JavaScript","JavaScript file",58,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"fortran77","Fortran fixed form","Fortran fixed form source file",59,"f77",L012,FOLD,{}},
 {"baanc","BaanC","BaanC File",60,"baan",L0_8,FOLD ";lexer.baan.styling.within.preprocessor=1;fold.preprocessor=1;fold.baan.syntax.based=1;fold.baan.keywords.based=1;fold.baan.sections=1;fold.baan.inner.level=1",{}},
 {"srec","S-Record","Motorola S-Record binary data",61,"srec","","",{}},
 {"ihex","Intel HEX","Intel HEX binary data",62,"ihex","","",{}},
 {"tehex","Tektronix extended HEX","Tektronix extended HEX binary data",63,"tehex","","",{}},
 {"swift","Swift","Swift file",64,"cpp",CPPFAM,CPPPROPS,{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"asn1","ASN.1","Abstract Syntax Notation One file",65,"asn1",L0123,FOLD,{}},
 {"avs","AviSynth","AviSynth scripts files",66,"avs",L012345,FOLD,{}},
 {"blitzbasic","BlitzBasic","BlitzBasic file",67,"blitzbasic",L0123,FOLD,{}},
 {"purebasic","PureBasic","PureBasic file",68,"purebasic",L0123,FOLD,{}},
 {"freebasic","FreeBasic","FreeBasic file",69,"freebasic",L0123,FOLD,{}},
 {"csound","Csound","Csound file",70,"csound",L012,FOLD,{}},
 {"erlang","Erlang","Erlang file",71,"erlang",L012345,FOLD,{}},
 {"escript","ESCRIPT","ESCRIPT file",72,"escript",L012,FOLD,{}},
 {"forth","Forth","Forth file",73,"forth",L012345,FOLD,{}},
 {"latex","LaTeX","LaTeX file",74,"latex","",FOLD,{}},
 {"mmixal","MMIXAL","MMIXAL file",75,"mmixal",L012,FOLD,{}},
 {"nim","Nim","Nim file",76,"nimrod",L0,FOLD,{}},
 {"nncrontab","Nncrontab","extended crontab file",77,"nncrontab",L012,FOLD,{}},
 {"oscript","OScript","OScript source file",78,"oscript",L012345,FOLD,{}},
 {"rebol","REBOL","REBOL file",79,"rebol",L0_6,FOLD,{}},
 {"registry","registry","registry file",80,"registry","",FOLD,{}},
 {"rust","Rust","Rust file",81,"rust",L0_6,FOLD,{}},
 {"spice","Spice","spice file",82,"spice",L012,FOLD,{}},
 {"txt2tags","txt2tags","txt2tags file",83,"txt2tags","",FOLD,{}},
 {"visualprolog","Visual Prolog","Visual Prolog file",84,"visualprolog",L0123,FOLD,{}},
 {"typescript","TypeScript","TypeScript file",85,"cpp","0:0,1:2,2:D",CPPPROPS ";lexer.cpp.backquoted.strings=1",{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"json5","json5","JSON5 file",86,"json",L01,"fold=1;fold.compact=0;lexer.json.escape.sequence=1;lexer.json.allow.comments=1",{}},
 {"mssql","mssql","Microsoft Transact-SQL (SQL Server) file",87,"mssql",L012345,FOLD,{}},
 {"gdscript","GDScript","GDScript file",88,"gdscript",L01,FOLD ";lexer.gdscript.keywords2.no.sub.identifiers=1;lexer.gdscript.whinge.level=1",{{SCE_GD_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"hollywood","Hollywood","Hollywood script",89,"hollywood",L0123,FOLD,{}},
 {"go","Go","Go source file",90,"cpp",CPPFAM,CPPPROPS ";lexer.cpp.backquoted.strings=1",{{SCE_C_IDENTIFIER,8,LANG_INDEX_SUBSTYLE1}}},
 {"raku","Raku","Raku source file",91,"raku",L0_6,FOLD ";fold.raku.comment.multiline=1;fold.raku.comment.pod=1",{}},
 {"toml","TOML","Tom's Obvious Minimal Language file",92,"toml",L0,FOLD,{}},
 {"sas","SAS","SAS file",93,"sas",L0123,FOLD,{}},
 {"errorlist","ErrorList","ErrorList file",94,"errorlist","","lexer.errorlist.value.separate=0;lexer.errorlist.escape.sequences=1",{}},
 {"escseq","EscapeSequence (ANSI)","Escape Sequence (ANSI) file",95,"escseq","","lexer.escseq.colour.text=1",{}},
 {"fcST","Structured Text","IEC 61131-3 Structured Text file",96,"fcST",L012345,FOLD ";fold.preprocessor=1",{}},
 // ---- Camelion++ additions
 {"markdown","Markdown","Markdown file",0x1000,"markdown","","",{}},
};

const std::vector<LangDef>& allLangs() { return g_langs; }

const LangDef* langByType(int t)
{
    for (const auto& l : g_langs) if (l.langType == t) return &l;
    return &g_langs[0];
}

const LangDef* langByName(const std::string& n)
{
    for (const auto& l : g_langs) if (n == l.name) return &l;
    return nullptr;
}

} // namespace cam
