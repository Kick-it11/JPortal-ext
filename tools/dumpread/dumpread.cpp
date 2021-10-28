#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcDesc.hpp"
#include "scopeDesc.hpp"
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <map>
#include <list>

using namespace std;
    enum DumpInfoType {
      _illegal = -1,
      _method_entry_initial,
      _method_entry,
      _method_exit,
      _compiled_method_load,
      _compiled_method_unload,
      _thread_start,
      _interpreter_info,
      _dynamic_code_generated,
      _inline_cache_add,
      _inline_cache_clear,
      _no_thing
    };
    struct DumpInfo {
        DumpInfoType type;
        /* total size: following MethodEntry/CompiledMethod size included*/
        uint64_t size;
        uint64_t time;
    };

    struct InterpreterInfo {
      bool TraceBytecodes;
      uint64_t codelets_address[3200];
    };

    struct MethodEntryInitial {
      int idx;
      uint64_t tid;
      int klass_name_length;
      int method_name_length;
      int method_signature_length;
    };

    struct MethodEntryInfo {
      int idx;
      uint64_t tid;
    };

    struct MethodExitInfo {
      int idx;
      uint64_t tid;
    };

    struct InlineMethodInfo {
        int klass_name_length;
        int name_length;
        int signature_length;
        int method_index;
    };

    struct ThreadStartInfo {
        long java_tid;
        long sys_tid;
    };

    struct CompiledMethodLoadInfo {
        uint64_t insts_begin;
        uint64_t insts_size;
        uint64_t scopes_pc_size;
        uint64_t scopes_data_size;
        uint64_t entry_point;
        uint64_t verified_entry_point;
        uint64_t osr_entry_point;
        int inline_method_cnt;
    };

    struct CompiledMethodUnloadInfo {
      uint64_t insts_begin;
    };

    struct DynamicCodeGenerated {
      int name_length;
      uint64_t code_begin;
      uint64_t code_size;
    };
    struct InlineCacheAdd {
      uint64_t src;
      uint64_t dest;
    };

    struct InlineCacheClear {
      uint64_t src;
    };
static int extract_base(char *arg, uint64_t *base) {
    char *sep, *rest;

    sep = strrchr(arg, ':');
    if (sep) {
        uint64_t num;

        if (!sep[1])
            return 0;

        errno = 0;
        num = strtoull(sep+1, &rest, 0);
        if (errno || *rest)
            return 0;

        *base = num;
        *sep = 0;
        return 1;
    }

    return 0;
}

static int parse_range(const char *arg, uint64_t *begin, uint64_t *end) {
    char *rest;

    if (!arg || !*arg)
        return 0;

    errno = 0;
    *begin = strtoull(arg, &rest, 0);
    if (errno)
        return -1;

    if (!*rest)
        return 1;

    if (*rest != '-')
        return -1;

    *end = strtoull(rest+1, &rest, 0);
    if (errno || *rest)
        return -1;

    return 2;
}

int preprocess_filename(char *filename, uint64_t *offset, uint64_t *size) {
    uint64_t begin, end;
    char *range;
    int parts;

    if (!filename || !offset || !size)
        return -1;

    /* Search from the end as the filename may also contain ':'. */
    range = strrchr(filename, ':');
    if (!range) {
        *offset = 0ull;
        *size = 0ull;

        return 0;
    }

    /* Let's try to parse an optional range suffix.
     *
     * If we can, remove it from the filename argument.
     * If we can not, assume that the ':' is part of the filename, e.g. a
     * drive letter on Windows.
     */
    parts = parse_range(range + 1, &begin, &end);
    if (parts <= 0) {
        *offset = 0ull;
        *size = 0ull;

        return 0;
    }

    if (parts == 1) {
        *offset = begin;
        *size = 0ull;

        *range = 0;

        return 0;
    }

    if (parts == 2) {
        if (end <= begin)
            return -1;

        *offset = begin;
        *size = end - begin;

        *range = 0;

        return 0;
    }

    return -1;
}

int load_file(uint8_t **buffer, size_t *psize, const char *filename,
             uint64_t offset, uint64_t size, const char *prog) {
    uint8_t *content;
    size_t read;
    FILE *file;
    long fsize, begin, end;
    int errcode;

    if (!buffer || !psize || !filename || !prog) {
        fprintf(stderr, "%s: internal error.\n", prog ? prog : "");
        return -1;
    }

    errno = 0;
    file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "%s: failed to open %s: %d.\n",
            prog, filename, errno);
        return -1;
    }

    errcode = fseek(file, 0, SEEK_END);
    if (errcode) {
        fprintf(stderr, "%s: failed to determine size of %s: %d.\n",
            prog, filename, errno);
        goto err_file;
    }

    fsize = ftell(file);
    if (fsize < 0) {
        fprintf(stderr, "%s: failed to determine size of %s: %d.\n",
            prog, filename, errno);
        goto err_file;
    }

    begin = (long) offset;
    if (((uint64_t) begin != offset) || (fsize <= begin)) {
        
        goto err_file;
    }

    end = fsize;
    if (size) {
        uint64_t range_end;

        range_end = offset + size;
        if ((uint64_t) end < range_end) {
            
            goto err_file;
        }

        end = (long) range_end;
    }

    fsize = end - begin;

    content = (uint8_t *)malloc((size_t) fsize);
    if (!content) {
        fprintf(stderr, "%s: failed to allocated memory %s.\n",
            prog, filename);
        goto err_file;
    }

    errcode = fseek(file, begin, SEEK_SET);
    if (errcode) {
        fprintf(stderr, "%s: failed to load %s: %d.\n",
            prog, filename, errno);
        goto err_content;
    }

    read = fread(content, (size_t) fsize, 1u, file);
    if (read != 1) {
        fprintf(stderr, "%s: failed to load %s: %d.\n",
            prog, filename, errno);
        goto err_content;
    }

    fclose(file);

    *buffer = content;
    *psize = (size_t) fsize;

    return 0;

err_content:
    free(content);

err_file:
    fclose(file);
    return -1;
}

class MethodDesc {
    int klass_name_length;
    int name_length;
    int signature_length;
    const char *klass_name;
    const char *name;
    const char *signature;

  public:  
    MethodDesc(int _klass_name_length = 0, int _name_length = 0,
                int _signature_length = 0, const char *_klass_name = nullptr,
                const char *_name = nullptr, const char *_signature = nullptr):
        klass_name_length(_klass_name_length),
        name_length(_name_length),
        signature_length(_signature_length),
        klass_name(_klass_name),
        name(_name),
        signature(_signature) {}
    void get_method_desc(std::string &_klass_name, std::string &_name, std::string &_signature) const {
      _klass_name = klass_name?std::string(klass_name, klass_name_length):"";
      _name = name?std::string(name, name_length):"";
      _signature = signature?std::string(signature, signature_length):"";
    }

};

int main(int argc, char **argv) {
  uint8_t *dump_info;
  size_t size;
  if (argc != 2) {
    fprintf(stderr, "wrong arg\n");
    return -1;
  }
  
  uint64_t foffset, fsize;
  int errcode;

  errcode = preprocess_filename(argv[1], &foffset, &fsize);
  if (errcode < 0) {
    fprintf(stderr, "bad file: %s.\n", argv[1]);
    return -1;
  }
  errcode = load_file(&dump_info, &size, argv[1], foffset, fsize, "main");
  if (errcode < 0) {
    fprintf(stderr, "bad file: %s.\n", argv[1]);
    return errcode;
  }
  const DumpInfo *info;
  char *klassname, *name, *sig;
  int count = 0;
  uint8_t *current = dump_info;
  uint8_t *end = dump_info + size;
  map<int, MethodDesc> method_map;
  list<pair<uint64_t, uint64_t>> memory;
  int cnt = 0;
  map<int, int> result;
  //FILE *met = fopen("inter_methods", "w");
  while (current < end) {
    info = (const struct DumpInfo *)current;
    current += sizeof(DumpInfo);
    switch(info->type) {
            case _interpreter_info: {
                const InterpreterInfo *ii;
                ii = (const InterpreterInfo *)current;
                current += sizeof(InterpreterInfo);
                printf("Inter %ld %ld\n", ii->codelets_address[2], ii->codelets_address[542]);
                break;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)current;
                current += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)current;
                current += me->klass_name_length;
                const char *name = (const char *)current;
                current += me->method_name_length;
                const char *signature = (const char *)current;
                current += me->method_signature_length;
                MethodDesc new_md(me->klass_name_length,
                        me->method_name_length, me->method_signature_length,
                        klass_name, name, signature);
                if (method_map.find(me->idx) != method_map.end()) {
                  fprintf(stderr, "override method entry\n");
                }
                string s1,s2,s3;
                new_md.get_method_desc(s1,s2,s3);
                printf("#entry %d %s %s %s\n", me->idx, s1.c_str(), s2.c_str(), s3.c_str());
                //fprintf(met, "%s %s%s : %d\n", s1.c_str(), s2.c_str(), s3.c_str(), me->idx);
                //printf("%d\n", me->idx);
                method_map[me->idx] = new_md;
                cnt++;
                result[me->idx]++;
                
                break;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)current;
                current += sizeof(MethodEntryInfo);
                auto iter = method_map.find(me->idx);
                if (iter == method_map.end())
                    fprintf(stderr, "no method entry\n");
                printf("#entry %d\n",me->idx);
                //printf("%d\n", me->idx);
                cnt++;
                result[me->idx]++;
                break;
            }
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)current;
                current += sizeof(MethodExitInfo);
                auto iter = method_map.find(me->idx);
                if (iter == method_map.end())
                    fprintf(stderr, "no method exit\n");
                printf("#exit %d\n", me->idx);
                break;
            }
            case _compiled_method_load: {
                const CompiledMethodLoadInfo *cm;
                cm = (const CompiledMethodLoadInfo*)current;
                current += sizeof(CompiledMethodLoadInfo);
                MethodDesc main_md;
                printf("#load:\n");
                bool ob = false;
                for (int i = 0; i < cm->inline_method_cnt; i++) {
                    const InlineMethodInfo*imi;
                    imi = (const InlineMethodInfo*)current;
                    current += sizeof(InlineMethodInfo);
                    const char *klass_name = (const char *)current;
                    current += imi->klass_name_length;
                    const char *name = (const char *)current;
                    current += imi->name_length;
                    const char *signature = (const char *)current;
                    current += imi->signature_length;
                    MethodDesc md(
                            imi->klass_name_length, imi->name_length,
                            imi->signature_length, klass_name, 
                            name, signature);
                    string s1,s2,s3;
                    md.get_method_desc(s1,s2,s3);
                    printf("\t%d %s %s %s %ld %ld(%ld)\n", i, s1.c_str(), s2.c_str(), s3.c_str(), cm->insts_begin, cm->insts_size, info->time);
                }
                const uint8_t *insts, *scopes_pc, *scopes_data;
                insts = (const uint8_t *)current;
                current += cm->insts_size;
                scopes_pc = (const uint8_t *)current;
                current += cm->scopes_pc_size;
                scopes_data = (const uint8_t *)current;
                current += cm->scopes_data_size;
                for (auto miter = memory.begin(); miter != memory.end();) {
                  if (miter->first >= cm->insts_begin && miter->first < cm->insts_begin + cm->insts_size || cm->insts_begin >= miter->first && cm->insts_begin < miter->first + miter->second)
                    memory.erase(miter++);
                  else
                    miter++;
                }
                if (cm->insts_begin == 139729395876448ul && cm->insts_size == 3088) {
                  FILE *fp = fopen("code", "wb");
                  fwrite(insts, cm->insts_size, 1, fp);
                  fclose(fp);
                }
                memory.push_front(make_pair(cm->insts_begin, cm->insts_size));
                break;
            }
            case _compiled_method_unload: {
                const CompiledMethodUnloadInfo *cm;
                cm = (const CompiledMethodUnloadInfo *)current;
                current += sizeof(CompiledMethodUnloadInfo);
                for (auto miter = memory.begin(); miter != memory.end(); miter++) {
                  if (miter->first == cm->insts_begin)
                    {
                      memory.erase(miter); break;
                    }
                }
                printf("#unload %ld\n", cm->insts_begin);
                break;
            }
            case _thread_start: {
                const ThreadStartInfo *th;
                th = (const ThreadStartInfo*)current;
                current += sizeof(ThreadStartInfo);
                printf("#thread start %ld %ld\n", th->java_tid, th->sys_tid);
                break;
            }
            case _dynamic_code_generated: {
                const DynamicCodeGenerated *dcg;
                dcg = (const DynamicCodeGenerated *)current;
                current += sizeof(DynamicCodeGenerated);
                const char *name;
                const uint8_t *code;
                name = (const char *)current;
                current += dcg->name_length;
                code = current;
                current += dcg->code_size;
                printf("#dynamic\n\t%s %ld %ld\n", name, dcg->code_begin, dcg->code_size);
                for (auto miter = memory.begin(); miter != memory.end();) {
                  if (miter->first >= dcg->code_begin && miter->first < dcg->code_begin + dcg->code_size || dcg->code_begin >= miter->first && dcg->code_begin < miter->first + miter->second)
                    memory.erase(miter++);
                  else
                    miter++;
                }
                memory.push_front(make_pair(dcg->code_begin, dcg->code_size));
                break;
            }
            case _inline_cache_add: {
                const InlineCacheAdd *ic;
                ic = (const InlineCacheAdd *)current;
                current += sizeof(InlineCacheAdd);
                printf("InlineCacheAdd: %ld %ld (%ld) ", ic->src, ic->dest, info->time);
                for (auto miter = memory.begin(); miter != memory.end(); miter++) {
                  if (ic->src >= miter->first && ic->src < miter->first + miter->second)
                    printf("(src : %ld %ld) ", miter->first, miter->second);
                  if (ic->dest >= miter->first && ic->dest < miter->first + miter->second)
                    printf("(dest : %ld %ld)", miter->first, miter->second);

                }
                printf("\n");
                break;
            }
            case _inline_cache_clear: {
                const InlineCacheClear *ic;
                ic = (const InlineCacheClear *)current;
                current += sizeof(InlineCacheClear);
                printf("InlineCacheClear: %ld (%ld) ", ic->src, info->time);
                for (auto miter = memory.begin(); miter != memory.end(); miter++) {
                  if (ic->src >= miter->first && ic->src < miter->first + miter->second)
                    printf("(src : %ld %ld) ", miter->first, miter->second);
                }
                printf("\n");
                break;
            }
            default: {
                current = end;
                fprintf(stderr, "wrong id %d\n", info->type);
                return _illegal;
            }
        }
    }
    //fclose(met);
    for (auto r : result) {
      printf("%d: %d\n", r.first, r.second);
    }
    printf("%d\n", cnt);
    return 0;
}
