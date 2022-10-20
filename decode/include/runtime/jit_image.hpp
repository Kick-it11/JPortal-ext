#ifndef JIT_IMAGE_HPP
#define JIT_IMAGE_HPP

#include "java/definitions.hpp"

#include <list>
#include <string>

class JitSection;

/* A traced image consisting of a collection of sections. */
class JitImage
{
private:
    /* The optional image name. */
    const std::string _name;

    /* The list of sections. */
    std::list<JitSection *> _sections;

    /* The list of removed sections. */
    std::list<JitSection *> _removed;

public:
    /** Traced memory image constructor. */
    JitImage(const std::string &name);

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
    bool remove(uint64_t vadrr);

    /* Find an image section.
     * Find the section containing @vaddr.
     * On success, return this @section
     */
    JitSection *find(uint64_t vaddr);

    /* Validate an image section.
     *
     * Validate that a lookup of @vaddr in @image.
     *
     * Validation may fail sporadically.
     *
     * Returns false if validation failed.
     */
    bool validate(JitSection *section, uint64_t vaddr);
};

#endif
