#ifndef JIT_IMAGE_HPP
#define JIT_IMAGE_HPP

#include <string>
#include <list>

using std::string;
using std::list;

class JitSection;

/* A traced image consisting of a collection of sections. */
class JitImage {
private:
    /* The optional image name. */
    const string _name;

    /* The list of sections. */
    list<JitSection *> _sections;

    /* The list of removed sections. */
    list<JitSection *> _removed;

public:
    /** Traced memory image constructor. */
    JitImage(const string &name);

    /** Traced memory image destructor. */
    ~JitImage();

    /* Add a section to an image.
     *
     * Add @section identified to @image at @vaddr.
     * If @section overlaps with existing sections,
     * the existing sections are shrunk, split, or
     * removed to accomodate @section.
     */
    void add(JitSection *section);

    /* Remove a section from an image that contains vaddr.
     * Returns true on success.
     */
    bool remove(address vadrr);

    /* Find an image section.
     * Find the section containing @vaddr.
     * On success, return this @section
     */
    JitSection *find(address vaddr);

    /* Validate an image section.
     *
     * Validate that a lookup of @vaddr in @image.
     *
     * Validation may fail sporadically.
     * 
     * Returns false if validation failed.
     */
    bool validate(JitSection *section, address vaddr);
};

#endif
