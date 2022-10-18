#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"

JitImage::JitImage(const std::string& name) : _name(name) {

}

JitImage::~JitImage() {

}

void JitImage::add(JitSection *section) {
    if (!section)
        return;

    uint64_t size = section->code_size();
    uint64_t begin = section->code_begin();
    uint64_t end = begin + size;

    /* Check for overlaps while we move to the end of the list. */
    std::list<JitSection*>::iterator it = _sections.begin();
    while (it != _sections.end()) {
        JitSection* lsec = *it;

        uint64_t lbegin = lsec->code_begin();
        uint64_t lend = lbegin + lsec->code_size();

        if ((end <= lbegin) || (lend <= begin)) {
            ++it;
            continue;
        }

        /* The new section overlaps with @msec's section. */

        /* We remove @sec and insert new sections for the remaining
         * parts, if any.  Those new sections are not mapped initially
         * and need to be added to the end of the section list.
         */
        it = _sections.erase(it);
        /* Keep a list of removed sections so we can re-add them in case
         * of errors.
         */
        _removed.push_back(lsec);
    }

    /* Add section to _sections */
    _sections.push_back(section);

    return;
}

bool JitImage::remove(address vaddr)
{
    std::list<JitSection*>::iterator it = _sections.begin();
    while(it != _sections.end()) {
        JitSection* sec = *it;

        uint64_t begin = sec->code_begin();
        uint64_t end = sec->code_size() + begin;

        /* if section sec contians address vaddr,
         * erase it from _sections and add it to _removed
         */
        if (vaddr >= begin && vaddr < end) {
            _sections.erase(it);
            _removed.push_back(sec);
            return true;
        }

        ++it;
    }

    return false;
}

JitSection *JitImage::find(address vaddr) {
    std::list<JitSection *>::iterator it = _sections.begin();
    while (it != _sections.end()) {
        JitSection *sec = *it;

        uint64_t begin = sec->code_begin();
        uint64_t end = sec->code_size() + begin;

        if (vaddr >= begin && vaddr < end) {
            return sec;
        }

        ++it;
    }

    return nullptr;
}
