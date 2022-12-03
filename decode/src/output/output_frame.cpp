#include "java/block.hpp"
#include "java/bytecodes.hpp"
#include "java/method.hpp"
#include "output/output_frame.hpp"
#include "runtime/jit_section.hpp"

#include <algorithm>
#include <cassert>
#include <list>
#include <queue>
#include <set>

InterFrame::InterFrame(const Method *method, int bci, bool use_next_bci)
{
    assert(method != nullptr);
    _method = method;
    _use_next_bct = use_next_bci;

    BlockGraph *bg = method->get_bg();
    _block = bg->block(bci);
    _bct = bg->bct_offset(bci);
    assert(_block != nullptr);
}

/* go forward until encoutering a branch/call site */
void InterFrame::forward(std::vector<uint8_t> &codes)
{
    BlockGraph *bg = _method->get_bg();
    const uint8_t *bctcode = bg->bctcode();
    while (_block)
    {
        if (_use_next_bct)
            ++_bct;
        int bct_end = _block->get_bct_codeend();
        for (int i = _bct; i < bct_end; ++i)
        {
            codes.push_back(bctcode[i]);
        }
        if (_bct < bct_end)
        {
            Bytecodes::Code b = Bytecodes::cast(bctcode[bct_end-1]);
            if (Bytecodes::is_invoke(b)
                || Bytecodes::is_branch(b)
                || b == Bytecodes::_tableswitch
                || b == Bytecodes::_lookupswitch)
            {
                _bct = bct_end-1;
                _use_next_bct = true;
                return;
            }
        }
        if (_block->get_succs_size() > 1)
        {
            return;
        }
        else if (_block->get_succs_size() == 1)
        {
            _block = *_block->get_succs_begin();            
            _bct = _block->get_bct_codebegin();
            _use_next_bct = false;
        }
        else
        {
            _block = nullptr;
            _bct = -1;
            _use_next_bct = false;
        }
    }
    return;
}

/* go forward until bct */
void InterFrame::forward(std::vector<uint8_t> &codes, int bct)
{
    BlockGraph *bg = _method->get_bg();
    const uint8_t *bctcode = bg->bctcode();
    while (_block)
    {
        if (_use_next_bct)
            ++_bct;
        int bct_begin = _block->get_bct_codebegin();
        int bct_end = _block->get_bct_codeend();
        if (bct >= bct_begin && bct < bct_end)
        {
            for (int i = _bct; i <= bct; ++i)
            {
                codes.push_back(bctcode[i]);
            }
            _bct = bct;
            _use_next_bct = false;
            return;
        }
        for (int i = _bct; i <= bct_end; ++i)
        {
            codes.push_back(bctcode[i]);
        }
        if (_bct < bct_end)
        {
            Bytecodes::Code b = Bytecodes::cast(bctcode[bct_end-1]);
            if (Bytecodes::is_invoke(b)
                || Bytecodes::is_branch(b)
                || b == Bytecodes::_tableswitch
                || b == Bytecodes::_lookupswitch)
            {
                _bct = bct_end-1;
                _use_next_bct = true;
                return;
            }
        }
        if (_block->get_succs_size() > 1)
        {
            return;
        }
        else if (_block->get_succs_size() == 1)
        {
            _block = *_block->get_succs_begin();            
            _bct = _block->get_bct_codebegin();
            _use_next_bct = false;
        }
        else
        {
            _block = nullptr;
            _bct = -1;
            _use_next_bct = false;
        }
    }
    return;
}

/* if normal exit/exception, return true;
 * else early return return false 
 */
bool InterFrame::method_exit(std::vector<uint8_t> &codes)
{
    if (!_block)
    {
        return true;
    }

    forward(codes);
    return _block == nullptr;
}

bool InterFrame::branch_taken(std::vector<uint8_t> &codes)
{
    if (!_block)
    {
        return false;
    }

    forward(codes);

    if (codes.empty() || !Bytecodes::is_branch(Bytecodes::cast(codes.back())))
    {
        return false;
    }

    _block = _block->get_succes_block(0);
    if (!_block)
        return false;
    _bct = _block->get_bct_codebegin();
    _use_next_bct = false;
    return true;
}

bool InterFrame::branch_not_taken(std::vector<uint8_t> &codes)
{
    if (!_block)
    {
        return false;
    }

    forward(codes);

    if (codes.empty() || !Bytecodes::is_branch(Bytecodes::cast(codes.back())))
    {
        return false;
    }

    _block = _block->get_succes_block(1);
    if (!_block)
        return false;
    _bct = _block->get_bct_codebegin();
    _use_next_bct = false;
    return true;
}

bool InterFrame::switch_case(std::vector<uint8_t> &codes, int index)
{
    if (!_block)
    {
        return false;
    }

    forward(codes);

    if (codes.empty() || (Bytecodes::cast(codes.back()) != Bytecodes::_lookupswitch
                    && Bytecodes::cast(codes.back()) != Bytecodes::_tableswitch))
    {
        return false;
    }

    _block = _block->get_succes_block(index+1);
    if (!_block)
        return false;
    _bct = _block->get_bct_codebegin();
    _use_next_bct = false;
    return true;
}

bool InterFrame::switch_default(std::vector<uint8_t> &codes)
{
    if (!_block)
    {
        return false;
    }

    forward(codes);

    if (codes.empty() || (Bytecodes::cast(codes.back()) != Bytecodes::_lookupswitch
                    && Bytecodes::cast(codes.back()) != Bytecodes::_tableswitch))
    {
        return false;
    }

    _block = _block->get_succes_block(0);
    if (!_block)
        return false;
    _bct = _block->get_bct_codebegin();
    _use_next_bct = false;
    return true;
}

bool InterFrame::invoke_site(std::vector<uint8_t> &codes)
{
    if (!_block)
    {
        return false;
    }

    forward(codes);

    if (codes.empty() || !Bytecodes::is_invoke(Bytecodes::cast(codes.back())))
    {
        return false;
    }

    return true;
}

bool InterFrame::exception_handling(std::vector<uint8_t> &codes, int bci1, int bci2)
{
    BlockGraph *bg = _method->get_bg();
    int bct = bg->bct_offset(bci1);
    forward(codes, bct);
    if (bct != _bct)
        return false;

    if (bci2 == bci1) {
        _block = nullptr;
        _bct = -1;
        _use_next_bct = false;
    } else {
        _block = bg->block(bci2);
        _bct = bg->bct_offset(bci1);
        _use_next_bct = false;
    }
    return true;
}

class JitMatchTree
{
private:
    const Method *method;
    std::map<Block *, int> seqs;
    JitMatchTree *father;
    std::map<Block *, JitMatchTree *> children;

    bool match_next(Block *cur, std::vector<std::pair<const Method *, Block *>> &ans)
    {
        std::vector<std::pair<int, Block *>> vv;
        std::unordered_set<Block *> ss;
        std::queue<std::pair<int, Block *>> q;
        std::vector<std::pair<int, int>> find_next;
        int find_end = -1;
        q.push({-1, cur});
        ss.insert(cur);
        while (!q.empty())
        {
            Block *blc = q.front().second;
            int idx = vv.size();
            vv.push_back({q.front().first, blc});
            q.pop();
            if (blc->get_succs_size() == 0)
            {
                if (find_end == -1)
                    find_end = idx;
                continue;
            }
            for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter)
            {
                if (ss.count(*iter))
                    continue;
                ss.insert(*iter);
                if (seqs.count(*iter))
                {
                    find_next.push_back({seqs[*iter], vv.size()});
                    vv.push_back({idx, *iter});
                }
                else
                {
                    q.push({idx, *iter});
                }
            }
        }
        std::list<std::pair<const Method *, Block *>> blocks;
        int idx = find_end;
        if (!find_next.empty())
        {
            idx = min_element(find_next.begin(), find_next.end())->second;
            seqs.erase(vv[idx].second);
        }
        while (idx > 0)
        {
            blocks.push_front({method, vv[idx].second});
            idx = vv[idx].first;
        }
        ans.insert(ans.end(), blocks.begin(), blocks.end());
        return !blocks.empty();
    }

    void skip_match(std::vector<std::pair<const Method *, Block *>> &frame, int idx,
                    std::set<std::pair<const Method *, Block *>> &notVisited,
                    std::vector<std::pair<const Method *, Block *>> &ans)
    {
        Block *cur = nullptr;
        if (idx >= frame.size())
        {
            frame.push_back({method, cur});
            notVisited.erase({method, cur});
        }
        else
        {
            cur = frame[idx].second;
            notVisited.erase({method, cur});
            if (idx != frame.size() - 1 && !children.count(cur))
            {
                for (int i = 0; i < frame.size() - idx - 1; ++i)
                    frame.pop_back();
            }
        }

        while (!seqs.empty())
        {
            auto iter = min_element(seqs.begin(), seqs.end(), [](std::pair<Block *, int> &&l, std::pair<Block *, int> &&r) -> bool
                                    { return l.second < r.second; });
            cur = iter->first;
            notVisited.erase({method, cur});
            seqs.erase(iter);
            if (children.count(cur))
                children[cur]->match(frame, idx + 1, notVisited, ans);
        }

        if (idx == frame.size() - 1)
            frame.pop_back();
    }

public:
    JitMatchTree(const Method *m, JitMatchTree *f) : method(m), father(f) {}
    ~JitMatchTree()
    {
        for (auto &&child : children)
            delete child.second;
    }

    bool insert(std::vector<std::pair<const Method *, Block *>> &execs, int seq, int idx)
    {
        if (idx >= execs.size())
            return true;
        const Method *m = execs[idx].first;
        Block *b = execs[idx].second;
        if (m != method)
            return false;
        if (idx < execs.size() - 1)
        {
            if (!children.count(b))
                children[b] = new JitMatchTree(execs[idx + 1].first, this);
            if (!children[b]->insert(execs, seq, idx + 1))
                return false;
        }
        if (!seqs.count(b))
            seqs[b] = seq;
        return true;
    }

    void match(std::vector<std::pair<const Method *, Block *>> &frame, int idx,
               std::set<std::pair<const Method *, Block *>> &notVisited,
               std::vector<std::pair<const Method *, Block *>> &ans)
    {
        notVisited.erase({method, nullptr});
        if (!method || !method->is_jportal())
            return skip_match(frame, idx, notVisited, ans);

        BlockGraph *bg = method->get_bg();
        Block *cur = nullptr;
        if (idx < frame.size() && frame[idx].first != method)
            return_frame(frame, frame.size() - idx, ans);
        if (idx >= frame.size())
        {
            cur = bg->block(0);
            frame.push_back({method, cur});
            ans.push_back({method, cur});
            notVisited.erase({method, cur});
        }
        else
        {
            cur = frame[idx].second;
            if (!cur)
                cur = bg->block(0);
            notVisited.erase({method, cur});
            if (idx < frame.size() - 1 && !children.count(cur))
                return_frame(frame, frame.size() - idx - 1, ans);
        }

        while (notVisited.size())
        {
            if (children.count(cur))
                children[cur]->match(frame, idx + 1, notVisited, ans);
            if (!notVisited.size())
                break;
            if (!match_next(cur, ans))
            {
                frame.pop_back();
                return;
            }
            else
            {
                cur = ans.back().second;
                frame[idx] = ans.back();
                notVisited.erase({method, cur});
            }
        }
    }

    static void return_method(const Method *method, Block *cur,
                              std::vector<std::pair<const Method *, Block *>> &ans)
    {
        if (!method || !method->is_jportal() || !cur)
            return;

        std::vector<std::pair<int, Block *>> vv;
        std::unordered_set<Block *> ss;
        std::queue<std::pair<int, Block *>> q;
        std::vector<std::pair<int, int>> find_next;
        int find_end = -1;
        q.push({0, cur});
        ss.insert(cur);
        while (!q.empty())
        {
            Block *blc = q.front().second;
            int idx = vv.size();
            vv.push_back({q.front().first, blc});
            q.pop();
            if (blc->get_succs_size() == 0)
                break;
            for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter)
            {
                if (ss.count(*iter))
                    continue;
                ss.insert(*iter);
                q.push({idx, *iter});
            }
        }
        std::list<std::pair<const Method *, Block *>> blocks;
        int idx = vv.size() - 1;
        while (idx > 0)
        {
            blocks.push_front({method, vv[idx].second});
            idx = vv[idx].first;
        }
        ans.insert(ans.end(), blocks.begin(), blocks.end());
        return;
    }

    static void return_frame(std::vector<std::pair<const Method *, Block *>> &frame, int count,
                             std::vector<std::pair<const Method *, Block *>> &blocks)
    {
        assert(count <= frame.size());
        for (int i = 0; i < count; ++i)
        {
            return_method(frame.back().first, frame.back().second, blocks);
            frame.pop_back();
        }
    }
};

bool JitFrame::jit_code(std::vector<uint8_t> &codes, const JitSection *section,
                        const PCStackInfo **pcs, uint64_t size, bool entry)
{
    std::set<const PCStackInfo *> pc_execs;
    std::set<std::pair<const Method *, Block *>> block_execs;
    bool notRetry = true;
    JitMatchTree *tree = new JitMatchTree(section->mainm(), nullptr);
    std::vector<std::pair<const Method *, Block *>> ans;
    if (entry)
    {
        assert(_iframes.empty());
        _iframes.push_back({section->mainm(), section->mainm()->get_bg()->block(0)});
        ans.push_back(_iframes.back());
    }
    auto &cur_frames = _iframes;
    auto call_match = [&cur_frames, &tree, &block_execs, &pc_execs, &ans, section](bool newtree) -> void
    {
        tree->match(cur_frames, 0, block_execs, ans);
        delete tree;
        tree = newtree ? new JitMatchTree(section->mainm(), nullptr) : nullptr;
        block_execs.clear();
        pc_execs.clear();
    };
    for (int i = 0; i < size; ++i)
    {
        const PCStackInfo *pc = pcs[i];
        if (pc_execs.count(pc))
            call_match(true);
        std::vector<std::pair<const Method *, Block *>> frame;
        for (int j = pc->numstackframes - 1; j >= 0; --j)
        {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            const Method *method = section->method(mi);
            Block *block = (method && method->is_jportal()) ?
                            method->get_bg()->block(bci) :
                            (Block *)(uint64_t)bci;
            frame.push_back({method, block});
            block_execs.insert({method, block});
        }
        if (_iframes.empty())
        {
            for (auto blc : frame)
                if (blc.first && blc.first->is_jportal() && blc.second)
                    ans.push_back(blc);
            _iframes = frame;
            block_execs.clear();
            notRetry = false;
        }
        else if (!tree->insert(frame, i, 0))
        {
            call_match(true);
            tree->insert(frame, i, 0);
        }
        pc_execs.insert(pc);
    }
    call_match(false);

    for (auto &&block: ans)
    {
        const uint8_t *bctcode = block.first->get_bg()->bctcode();
        int bct_begin = block.second->get_bct_codebegin();
        int bct_end = block.second->get_bct_codeend();
        for (int i = bct_begin; i < bct_end; ++i)
            codes.push_back(bctcode[i]);
    }
    return notRetry;
}

bool JitFrame::jit_return(std::vector<u1> &codes)
{
    std::vector<std::pair<const Method *, Block *>> ans;
    JitMatchTree::return_frame(_iframes, _iframes.size(), ans);

    for (auto &&block: ans)
    {
        const uint8_t *bctcode = block.first->get_bg()->bctcode();
        int bct_begin = block.second->get_bct_codebegin();
        int bct_end = block.second->get_bct_codeend();
        for (int i = bct_begin; i < bct_end; ++i)
            codes.push_back(bctcode[i]);
    }
    return true;
}
