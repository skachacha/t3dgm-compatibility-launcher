#include "editor_patch.hpp"
#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>

void read_remote(HANDLE, std::uintptr_t, void*, SIZE_T);
void write_remote(HANDLE, std::uintptr_t, const void*, SIZE_T);

namespace {
struct Ref { unsigned kind; std::uint32_t value; };
struct Builder {
    std::uint32_t arena, extension;
    std::vector<std::uint32_t> code, cells;
    std::map<std::string,std::uint32_t> variables, labels, label_ids;
    struct Fix { std::size_t word; Ref ref; };
    std::vector<Fix> fixes;

    Ref a(std::uint32_t slot) const { return {0,arena+slot*4}; }
    Ref v(const std::string& name, std::uint32_t initial=0) {
        auto found=variables.find(name);
        if (found!=variables.end()) return {1,found->second};
        auto at=std::uint32_t(cells.size()); cells.push_back(initial); variables.emplace(name,at);
        return {1,at};
    }
    Ref k(std::uint32_t value) { return v("constant:"+std::to_string(value),value); }
    Ref l(const std::string& name) {
        auto found=label_ids.find(name);
        if (found!=label_ids.end()) return {2,found->second};
        auto at=std::uint32_t(label_ids.size());label_ids.emplace(name,at);return {2,at};
    }
    void label(const std::string& name) {
        auto id=l(name).value;
        need(labels.emplace(std::to_string(id),std::uint32_t(code.size())).second,"Duplicate patch label");
    }
    void emit(std::uint32_t opcode, std::initializer_list<Ref> operands={}) {
        code.push_back(opcode);
        for (auto ref:operands) {
            if (ref.kind) fixes.push_back({code.size(),ref});
            code.push_back(ref.value);
        }
    }
    void call(const std::string& name) { emit(41,{l(name)}); }
    void jump(const std::string& name) { emit(2,{l(name)}); }
    void zero(Ref value, const std::string& name) { emit(1,{value,l(name)}); }
    std::uint32_t address(const std::string& name) { return extension+4*labels.at(std::to_string(l(name).value)); }
    std::vector<std::uint32_t> finish() {
        auto data_at=std::uint32_t(code.size());
        for (auto fix:fixes) {
            auto offset=fix.ref.kind==1 ? data_at+fix.ref.value : labels.at(std::to_string(fix.ref.value));
            code[fix.word]=extension+4*offset;
        }
        code.insert(code.end(),cells.begin(),cells.end());return code;
    }
};
}

std::vector<std::uint32_t> relocate_editor_code(const EditorProgram& program,
    std::uint32_t code, std::uint32_t arena) {
    auto result=program.code;
    for (auto& word:result) {
        if (word>=10000 && word<program.arena_token_limit) word=arena+4*(word-10000);
        else if (word>=program.inline_token_base && word<program.inline_token_base+10000) word-=program.inline_token_base;
        else if (word>=program.code_token_base) word=code+4*(word-program.code_token_base);
    }
    return result;
}

EditorPatchImage build_editor_patch(const EditorProgram& program,
    std::uint32_t code, std::uint32_t arena, std::uint32_t extension) {
    Builder b{arena,extension,{},{},{},{},{},{}};
    auto selected=b.v("selected"), catalog=b.v("catalog"), genre=b.v("genre"), course=b.v("course");
    auto prefix=b.v("prefix"), epos=b.v("epos"), elements=b.v("elements");

    // The selected definition supplies both halves of the saved scene identity.
    b.label("canonical");
    b.emit(671,{b.a(2001),b.a(7003),b.k(95)});
    b.emit(248,{b.a(2000),b.a(2001),b.k(0)});
    b.emit(661,{b.a(0),b.a(3164),b.k(0)});b.emit(151,{selected,b.a(0)});
    b.emit(111,{b.a(0),selected,b.k(0)});b.zero(b.a(0),"canonical_done");
    b.emit(117,{b.a(0),selected,b.k(100)});b.zero(b.a(0),"canonical_done");
    b.emit(662,{b.a(0),b.a(3183),b.k(1),selected});b.emit(151,{catalog,b.a(0)});
    b.emit(111,{b.a(0),catalog,b.k(0)});b.zero(b.a(0),"canonical_done");
    b.emit(117,{b.a(0),catalog,b.a(3493)});b.zero(b.a(0),"canonical_done");
    b.emit(661,{b.a(0),b.a(3400),catalog});
    b.emit(101,{b.a(1),b.a(0),b.k(1)});b.zero(b.a(1),"canonical_done");
    b.emit(661,{b.a(0),b.a(3401),catalog});b.emit(151,{genre,b.a(0)});
    b.emit(111,{b.a(0),genre,b.k(0)});b.zero(b.a(0),"canonical_done");
    b.emit(117,{b.a(0),genre,b.k(10)});b.zero(b.a(0),"canonical_done");
    b.emit(671,{b.a(2002),b.a(7086),genre});
    b.emit(87,{b.a(2003),b.a(2001),b.a(2002)});
    b.emit(671,{b.a(2002),b.a(7003),b.k(381)});
    b.emit(87,{b.a(2004),b.a(2003),b.a(2002)});
    b.emit(671,{b.a(2002),b.a(7087),catalog});
    b.emit(87,{b.a(2003),b.a(2004),b.a(2002)});
    b.emit(248,{b.a(2002),b.a(2001),b.k(1)});
    b.emit(87,{b.a(2000),b.a(2003),b.a(2002)});
    b.label("canonical_done");b.emit(43);

    b.label("resources");
    b.call("canonical");
    b.emit(234,{b.a(0),b.a(2000)});
    b.emit(111,{b.a(1),b.a(0),b.k(1)});b.zero(b.a(1),"collect_original");
    b.emit(661,{b.a(0),b.a(3184),selected});
    b.emit(111,{b.a(1),b.a(0),b.k(0)});b.zero(b.a(1),"collect_original");
    b.emit(153,{prefix,b.a(2000)});b.call("pair");
    b.zero(b.a(3961),"courses_begin");b.jump("resources_done");
    b.label("courses_begin");b.emit(151,{course,b.k(0)});
    b.label("courses_loop");
    b.emit(672,{b.a(2000),b.a(7032),selected,course});
    b.emit(153,{prefix,b.a(2000)});b.call("pair");
    b.zero(b.a(3961),"courses_next");b.jump("resources_done");
    b.label("courses_next");b.emit(81,{b.a(0),course,b.k(1)});b.emit(151,{course,b.a(0)});
    b.emit(117,{b.a(0),course,b.k(20)});b.zero(b.a(0),"collect_original");
    b.jump("courses_loop");
    b.label("collect_original");b.emit(41,{{0,code+4*(81279+program.word_shift)}});
    b.label("resources_done");b.emit(43);

    b.label("pair");
    b.emit(234,{b.a(0),prefix});
    b.emit(111,{b.a(1),b.a(0),b.k(1)});b.zero(b.a(1),"pair_done");
    b.emit(671,{b.a(2000),b.a(7003),b.k(400)});
    b.emit(87,{b.a(2001),prefix,b.a(2000)});b.emit(153,{epos,b.a(2001)});
    b.emit(671,{b.a(2000),b.a(7003),b.k(401)});
    b.emit(87,{b.a(2001),prefix,b.a(2000)});b.emit(153,{elements,b.a(2001)});
    b.emit(8121,{b.a(0),epos});b.emit(8121,{b.a(1),elements});
    b.emit(101,{b.a(2),b.a(0),b.a(1)});b.zero(b.a(2),"pair_fail");
    b.zero(b.a(0),"pair_done");
    b.emit(111,{b.a(0),b.a(4032),b.k(0)});b.zero(b.a(0),"pair_fail");
    b.emit(81,{b.a(0),b.a(4032),b.k(2)});
    b.emit(117,{b.a(1),b.a(0),b.k(program.path_capacity)});b.zero(b.a(1),"pair_fail");
    b.emit(571,{b.a(7220),b.a(4032),epos});b.emit(412,{b.a(4032)});
    b.emit(571,{b.a(7220),b.a(4032),elements});b.emit(412,{b.a(4032)});
    b.jump("pair_done");
    b.label("pair_fail");b.emit(151,{b.a(3961),b.k(1)});
    b.label("pair_done");b.emit(43);

    EditorPatchImage result;
    result.canonical_entry=b.address("canonical");result.resource_entry=b.address("resources");
    result.words=b.finish();
    auto relocated=relocate_editor_code(program,code,arena);
    auto edit=[&](std::uint32_t original, std::vector<std::uint32_t> replacement) {
        auto word=original+program.word_shift;
        need(word+replacement.size()<=relocated.size(),"Editor patch exceeds program boundary");
        result.edits.push_back({word,{relocated.begin()+word,relocated.begin()+word+replacement.size()},std::move(replacement)});
    };
    for (auto word:{77933u,77952u}) {
        auto shifted=word+program.word_shift;
        need(program.code.at(shifted)==671 && program.code.at(shifted+1)==12000 &&
             program.code.at(shifted+2)==17008 && program.code.at(shifted+3)==19000,"Editor scene load signature differs");
        edit(word,{41,result.canonical_entry,2,code+4*(shifted+4)});
    }
    need(program.code.at(78122+program.word_shift)==661,"Editor layout collection signature differs");
    edit(78122,{2,code+4*(78175+program.word_shift)});
    need(program.code.at(77842+program.word_shift)==41 &&
         program.code.at(77843+program.word_shift)==program.code_token_base+81279+program.word_shift,
         "Editor resource entry signature differs");
    edit(77843,{result.resource_entry});
    return result;
}

std::wstring apply_editor_patch(HANDLE process, const EditorProgram& program) {
    auto read_word=[&](std::uint32_t rva) {
        std::uint32_t value=0;read_remote(process,PreferredBase+rva,&value,4);return value;
    };
    auto code=read_word(program.code_pointer_rva), arena=read_word(program.arena_pointer_rva);
    need(code!=0 && arena!=0 && read_word(program.code_size_rva)==program.code.size()*4,"Editor program allocation differs");
    auto expected=relocate_editor_code(program,code,arena);
    std::vector<std::uint32_t> actual(expected.size());
    read_remote(process,code,actual.data(),actual.size()*4);
    need(actual==expected,"Loaded editor program differs from the identified program");
    void* allocation=VirtualAllocEx(process,nullptr,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    win32_need(allocation!=nullptr,"Cannot allocate editor save correction");
    try {
        auto address=reinterpret_cast<std::uintptr_t>(allocation);
        need(address<=0xffffffffu-65536,"Editor correction address is outside x86 range");
        auto image=build_editor_patch(program,code,arena,std::uint32_t(address));
        need(image.words.size()*4<=65536,"Editor correction exceeds allocation");
        write_remote(process,address,image.words.data(),image.words.size()*4);
        std::vector<std::uint32_t> checked(image.words.size());
        read_remote(process,address,checked.data(),checked.size()*4);
        need(checked==image.words,"Editor correction readback differs");
        for (const auto& edit:image.edits) {
            std::vector<std::uint32_t> before(edit.expected.size());
            read_remote(process,code+4*edit.word,before.data(),before.size()*4);
            need(before==edit.expected,"Editor save instruction changed during startup");
        }
        for (const auto& edit:image.edits) {
            write_remote(process,code+4*edit.word,edit.replacement.data(),edit.replacement.size()*4);
            std::vector<std::uint32_t> after(edit.replacement.size());
            read_remote(process,code+4*edit.word,after.data(),after.size()*4);
            need(after==edit.replacement,"Editor save instruction readback differs");
        }
        return L"Editor multilevel save correction applied and verified ("+
            std::to_wstring(image.words.size()*4)+L" bytes of VM instructions/data; disk EXE unchanged).";
    } catch (...) {
        VirtualFreeEx(process,allocation,0,MEM_RELEASE);throw;
    }
}
