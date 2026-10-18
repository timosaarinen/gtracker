#include "gtracker/editor_recovery.hpp"
#include "gtracker/io.hpp"
#include "gtracker/hash.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<typename F> void rejects(F function) { try { function(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected recovery failure"); }
void tempo(gtracker::EditorDocument& document,double value) { auto next=document.pattern(); next.bpm=value; document.commit(std::move(next)); }
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected original fixture root");
        const auto directory=gtracker::unique_sibling_path(std::filesystem::current_path()/"recovery-test","owned");
        check(std::filesystem::create_directory(directory),"Could not exclusively own recovery test directory");
        auto pattern=gtracker::load_pattern(std::filesystem::path(argv[1])/"examples/slide-study.gtrk.json");
        pattern.production=gtracker::ProductionSettings{}; pattern.production->enabled=true;
        const auto original=directory/"original.gtrk.json"; gtracker::save_pattern(pattern,original);
        const auto original_hash=gtracker::sha256_file(original);
        gtracker::EditorDocument document(pattern,original);
        gtracker::RecoveryCopies copies(directory/"data"), other(directory/"data");
        check(!copies.update(document,0) && copies.list().copies.empty(),"Clean document created a recovery copy");
        tempo(document,100);
        check(!copies.update(document,4.99),"Recovery did not wait five seconds");
        check(copies.update(document,5.01),"Dirty document was not checkpointed");
        auto first=copies.owned_path();
        check(copies.contains(first) && gtracker::pattern_json(gtracker::load_pattern(first))==gtracker::pattern_json(document.pattern()),"Recovery lost curves/production/timing/revision");
        check(gtracker::sha256_file(original)==original_hash,"Checkpoint overwrote the original document");
        const auto modified=std::filesystem::last_write_time(first);
        check(!copies.update(document,10.1) && std::filesystem::last_write_time(first)==modified,"Unchanged dirty score was rewritten");
        check(other.update(document,6) && other.owned_path()!=first,"Two sessions shared one recovery filename");
        const auto other_path=other.owned_path(); const auto other_hash=gtracker::sha256_file(other_path);
        tempo(document,110); check(copies.update(document,10.2),"Changed score did not update its owned copy");
        check(gtracker::load_pattern(first).bpm==110 && gtracker::sha256_file(other_path)==other_hash,"Checkpoint changed another session's copy");
        document.save(original); copies.update(document,20);
        const auto saved_hash=gtracker::sha256_file(original);
        check(!std::filesystem::exists(first) && std::filesystem::exists(other_path),"Successful save cleaned the wrong session");
        tempo(document,115); check(copies.update(document,26),"New unsaved work did not create a fresh copy");
        const auto good=copies.owned_path(); const auto good_hash=gtracker::sha256_file(good);
        auto dense=document.pattern(); dense.events.clear(); dense.gestures.clear();
        for (int i=0;i<5000;++i) dense.events.push_back({std::string(100,'a')+std::to_string(i),"pluck",i/1000.0,0,0,.5f});
        check(gtracker::pattern_json(dense).size()>1024*1024,"Oversized test fixture was not oversized");
        document.commit(dense);
        rejects([&] { copies.update(document,32); });
        check(gtracker::sha256_file(good)==good_hash && !copies.update(document,33),"Failed checkpoint replaced old work or retried every frame");
        check(document.undo(),"Undo oversized test data"); check(copies.update(document,38),"Recovery did not recover after a failed write");
        rejects([&] { copies.update(document,-1); });
        rejects([&] { copies.update(document,std::numeric_limits<double>::quiet_NaN()); });
        { std::ofstream bad(copies.directory()/"recovery.future.gtrk.json"); bad << "{\"schema_version\":99}"; }
        const auto listing=copies.list();
        check(listing.copies.size()==1 && listing.unreadable==1 && std::filesystem::exists(copies.directory()/"recovery.future.gtrk.json"),"Listing lost a session, loaded unsupported data or removed unreadable work");
        const auto suggested=copies.suggested_save_path();
        check(suggested.parent_path()==std::filesystem::weakly_canonical(directory/"data") && !std::filesystem::exists(suggested) && !copies.contains(suggested),"Recovered save suggestion aliases an automatic copy");
        const auto preserved=copies.owned_path(); copies.clear_owned(preserved);
        check(copies.owned_path().empty() && std::filesystem::exists(preserved),"Opening an owned recovery removed its only disk copy");
        other.clear_owned(); check(!std::filesystem::exists(other_path) && std::filesystem::exists(preserved),"Clearing another instance removed the preserved copy");
        const auto blocker=directory/"blocked"; { std::ofstream file(blocker); file << "owned write-failure fixture"; }
        gtracker::RecoveryCopies blocked(blocker); rejects([&] { blocked.update(document,6); });
        check(gtracker::sha256_file(original)==saved_hash && std::filesystem::is_regular_file(blocker),"Write failure damaged unrelated files");
        const auto casing=directory/"case-data";
        std::filesystem::create_directories(casing/"Recovery");
        gtracker::RecoveryCopies case_copy(casing);
        check(case_copy.update(document,6),"Preexisting differently cased recovery folder blocked checkpointing");
        const auto expected=std::filesystem::exists(casing/"recovery") ? std::filesystem::weakly_canonical(casing/"recovery") : casing/"recovery";
        check(case_copy.owned_path().parent_path()==expected,"Case handling changed the recovery destination");
        check(gtracker::pattern_json(gtracker::load_pattern(case_copy.owned_path()))==gtracker::pattern_json(document.pattern()),"Case-handled recovery lost score data");
        case_copy.clear_owned();
        check(std::filesystem::weakly_canonical(directory).parent_path()==std::filesystem::current_path(),"Owned recovery cleanup escaped its build directory");
        std::filesystem::remove_all(directory);
        std::cout << "recovery: interval, exact score copies, session ownership, save cleanup, failure preservation, bounded listing and safe reopen passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
