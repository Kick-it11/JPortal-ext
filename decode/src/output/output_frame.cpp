#include "java/analyser.hpp"
#include "java/block.hpp"
#include "java/bytecodes.hpp"
#include "java/klass.hpp"
#include "java/method.hpp"
#include "output/output_frame.hpp"
#include "runtime/jit_section.hpp"

#include <algorithm>
#include <cassert>
#include <list>
#include <queue>
#include <set>

InterFrame::InterFrame(const Method *method, int bci)
{
    _method = method;
    assert(method != nullptr && method->is_jportal());
    _bci = bci;
    _block = _method->get_bg()->offset2block(bci);
    assert(_block != nullptr);
    _pending = false;
    _jit_invoke = false;
}

Bytecodes::Code InterFrame::code()
{
    return _method->get_bg()->code(_bci);
}

const Method *InterFrame::static_callee()
{
    auto methodref = _method->get_klass()->index2methodref(_method->get_bg()->method_ref(_bci));
    if (code() == Bytecodes::_invokestatic)
        return Analyser::get_method(methodref.first, methodref.second);
    if (code() == Bytecodes::_invokespecial)
    {
        const Klass *klass = Analyser::get_klass(methodref.first);
        while (klass)
        {
            const Method *callee = klass->get_method(methodref.second);
            if (callee)
                return callee;
            klass = Analyser::get_klass(klass->get_father_name());
        }
    }
    return nullptr;
}

void InterFrame::set_jit_invoke()
{
    if (Bytecodes::is_invoke(code()))
    {
        _jit_invoke = true;
    }
}

bool InterFrame::taken()
{
    if (!Bytecodes::is_branch(code()) || !_block)
        return false;
    _block = _block->get_succes_block(0);
    if (!_block)
        return false;
    _bci = _block->get_begin_bci();
    _pending = false;
    return true;
}

bool InterFrame::not_taken()
{
    if (!Bytecodes::is_branch(code()) || !_block)
        return false;
    _block = _block->get_succes_block(1);
    if (!_block)
        return false;
    _bci = _block->get_begin_bci();
    _pending = false;
    return true;
}

bool InterFrame::switch_case(int idx)
{
    if ((Bytecodes::_tableswitch != code() && Bytecodes::_lookupswitch != code()) || !_block)
        return false;
    _block = _block->get_succes_block(idx);
    if (!_block)
        return false;
    _bci = _block->get_begin_bci();
    _pending = false;
    return true;
}

bool InterFrame::switch_default()
{
    if ((Bytecodes::_tableswitch != code() && Bytecodes::_lookupswitch != code()) || !_block)
        return false;
    _block = _block->get_succes_block(_block->get_succs_size() - 1);
    _bci = _block->get_begin_bci();
    if (!_block)
        return false;
    _pending = false;
    return true;
}

bool InterFrame::invoke()
{
    if (!Bytecodes::is_invoke(code()) || !_block || !_block->get_succs_size() == 1)
        return false;
    _block = (*_block->get_succs_begin());
    _bci = _block->get_begin_bci();
    _pending = false;
    _jit_invoke = false;
    return true;
}

/* go forward until encoutering a branch/call site */
bool InterFrame::forward(std::vector<std::pair<int, int>> &ans)
{
    if (!_block)
        return false;

    for (;;)
    {
        if (_bci != _block->get_end_bci() || !_pending)
            ans.push_back({_bci, _block->get_end_bci()});
        _bci = _block->get_end_bci();
        Bytecodes::Code cur = code();
        if (Bytecodes::is_branch(cur) || Bytecodes::is_invoke(cur) ||
            Bytecodes::_ret == cur ||
            Bytecodes::_tableswitch == cur || Bytecodes::_lookupswitch == cur)
        {
            _pending = true;
            return true;
        }
        /* for return or athrow */
        if (0 == _block->get_succs_size())
            return true;
        if (_block->get_succs_size() > 1)
            return false;
        _block = *(_block->get_succs_begin());
        _bci = _block->get_begin_bci();
        _pending = false;
    }
}

/* go forward to bci, exception throw/popframe/earlyret/ */
bool InterFrame::forward(int idx, std::vector<std::pair<int, int>> &ans)
{
    if (!_block)
        return false;

    for (;;)
    {
        if (_block->get_begin_bci() <= idx || _block->get_end_bci() >= idx)
        {
            if (_bci != _block->get_end_bci() || !_pending)
                ans.push_back({_bci, idx});
            _bci = idx;
            _pending = true; /* forward to bci always needs to be handled */
            return true;
        }
        if (_bci != _block->get_end_bci() || !_pending)
            ans.push_back({_bci, _block->get_end_bci()});
        _bci = _block->get_end_bci();
        Bytecodes::Code cur = code();
        if (Bytecodes::is_invoke(cur) || Bytecodes::is_branch(cur) ||
            Bytecodes::_tableswitch == cur || Bytecodes::_lookupswitch == cur)
        {
            _pending = true;
            return false;
        }
        if (0 == _block->get_succs_size())
            return false;
        if (_block->get_succs_size() > 1)
            return false;
        _block = *(_block->get_succs_begin());
        _bci = _block->get_begin_bci();
        _pending = false;
    }
}

/* exception handling/ret */
bool InterFrame::straight(int idx)
{
    _bci = idx;
    _pending = false;
    _block = _method->get_bg()->offset2block(idx);
    return _block != nullptr;
}

/* Sim Jit execution based on debug info
 * Error Might happen, but
 * we do not let influence inter
 * In a control flow level
 */
class SimNode
{
private:
    const Method *const _method;
    std::map<Block *, int> _marks;
    std::map<Block *, SimNode *> _children;

    void traverse_empty(std::vector<std::pair<const Method *, Block *>> &iframes, int idx,
                        std::set<std::pair<const Method *, Block *>> totals,
                        bool cfgt, bool mt,
                        std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                        std::vector<std::pair<std::string, int>> &methods)
    {
        /* Sometimes bci = -1 */
        totals.erase({_method, nullptr});

        /* Should return iframes (>= idx) when iframe[idx] is not this method */
        if (idx < iframes.size() && iframes[idx].first != _method)
            return_iframes(iframes, iframes.size()-idx, cfgt, mt, cfgs, methods);

        Block *cur = nullptr;
        assert(idx <= iframes.size());
        if (idx == iframes.size())
        {
            iframes.push_back({_method, cur});
            totals.erase({_method, cur});
        }
        else
        {
            cur = iframes[idx].second;
            totals.erase({_method, cur});
            /* Should erase latter(> idx) frames */
            if (idx < iframes.size() - 1 && !_children.count(cur))
                return_iframes(iframes, iframes.size()-idx-1, cfgt, mt, cfgs, methods);
        }

        while (!_marks.empty())
        {
            auto iter = min_element(_marks.begin(), _marks.end(),
                                    [](std::pair<Block *, int> &&l,
                                       std::pair<Block *, int> &&r) -> bool
                                    { return l.second < r.second; });
            cur = iter->first;
            totals.erase({_method, cur});
            iframes[idx] = {_method, cur};
            _marks.erase(iter);
            if (_children.count(cur))
                _children[cur]->traverse(iframes, idx+1, totals, cfgt, mt, cfgs, methods);
        }

        return;
    }

    Block *next_block(Block *cur, bool cfgt, std::vector<std::pair<int, std::pair<int, int>>> &cfgs)
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
                if (_marks.count(*iter))
                {
                    find_next.push_back({_marks[*iter], vv.size()});
                    vv.push_back({idx, *iter});
                }
                else
                {
                    q.push({idx, *iter});
                }
            }
        }
        std::list<Block *> blocks;
        int idx = find_end;
        if (!find_next.empty())
        {
            idx = min_element(find_next.begin(), find_next.end())->second;
            _marks.erase(vv[idx].second);
        }
        while (idx > 0)
        {
            blocks.push_front(vv[idx].second);
            idx = vv[idx].first;
        }
        /* Add to ans */
        for (auto block : blocks)
            if (cfgt)
                cfgs.push_back({_method->id(), {block->get_begin_bci(), block->get_end_bci()}});
        return blocks.empty() ? nullptr : blocks.back();
    }

public:
    SimNode(const Method *m) : _method(m) {}
    ~SimNode() { for (auto child : _children) delete  child.second; }

    void mark(std::vector<std::pair<const Method *, Block *>> &info, int time, int idx)
    {
        if (idx >= info.size())
            return;
        const Method *m = info[idx].first;
        Block *cur = info[idx].second;
        assert(m == _method);
        if (idx < info.size() - 1)
        {
            if (!_children.count(cur))
                _children[cur] = new SimNode(info[idx + 1].first);
            _children[cur]->mark(info, time, idx + 1);
        }
        if (!_marks.count(cur))
            _marks[cur] = time;
        return;
    }

    void traverse(std::vector<std::pair<const Method *, Block *>> &iframes,  int idx,
                  std::set<std::pair<const Method *, Block *>> totals,
                  bool cfgt, bool mt,
                  std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                  std::vector<std::pair<std::string, int>> &methods)
    {
        if (!_method || !_method->is_jportal())
            return traverse_empty(iframes, idx, totals, cfgt, mt, cfgs, methods);

        /* Sometimes bci = -1 */
        totals.erase({_method, nullptr});

        /* Should return iframes (>= idx) when iframe[idx] is not this method */
        if (idx < iframes.size() && iframes[idx].first != _method)
            return_iframes(iframes, iframes.size()-idx, cfgt, mt, cfgs, methods);

        BlockGraph *bg = _method->get_bg();
        Block *cur = nullptr;
        assert(idx <= iframes.size());
        if (idx == iframes.size())
        {
            cur = bg->offset2block(0);
            iframes.push_back({_method, cur});
            /* add cur to ans */
            if (cfgt) cfgs.push_back({_method->id(), {cur->get_begin_bci(), cur->get_end_bci()}});
            if (mt) methods.push_back({"i", _method->id()});
            totals.erase({_method, cur});
        }
        else
        {
            cur = iframes[idx].second;
            if (cur == nullptr)
            {
                /* iframe from first bci might have block empty */
                cur = bg->offset2block(0);
                iframes.push_back({_method, cur});
                /* add cur to ans */
                if (cfgt) cfgs.push_back({_method->id(), {cur->get_begin_bci(), cur->get_end_bci()}});
                if (mt) methods.push_back({"i", _method->id()});
                totals.erase({_method, cur});
            }
            totals.erase({_method, cur});
            /* Should erase latter(> idx) frames */
            if (idx < iframes.size() - 1 && !_children.count(cur))
                return_iframes(iframes, iframes.size()-idx-1, cfgt, mt, cfgs, methods);
        }

        while (totals.size())
        {
            if (_children.count(cur))
                _children[cur]->traverse(iframes, idx + 1, totals, cfgt, mt, cfgs, methods);
            if (!totals.size())
                break;
            cur = next_block(cur, cfgt, cfgs);
            if (!cur)
            {
                return return_iframes(iframes, iframes.size()-idx, cfgt, mt,  cfgs, methods);
            }
            else
            {
                iframes[idx] = {_method, cur};
                totals.erase({_method, cur});
            }
        }
    }

    static void return_iframe(const Method *method, Block *cur,
                              bool cfgt, bool mt,
                              std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                              std::vector<std::pair<std::string, int>> &methods)
    {
        if (!method || !method->is_jportal())
            return;

        assert(cur != nullptr);
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
        std::list<Block *> blocks;
        int idx = vv.size() - 1;
        while (idx > 0)
        {
            blocks.push_front(vv[idx].second);
            idx = vv[idx].first;
        }
        /* add to ans */
        for (auto block : blocks)
            if (cfgt)
                cfgs.push_back({method->id(), {block->get_begin_bci(), block->get_end_bci()}});
        if (mt) methods.push_back({"o", method->id()});
        return;
    }

    static void return_iframes(std::vector<std::pair<const Method *, Block *>> &iframes, int count,
                               bool cfgt, bool mt,
                               std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                               std::vector<std::pair<std::string, int>> &methods)
    {
        assert(count <= iframes.size());
        for (int i = 0; i < count; ++i)
        {
            return_iframe(iframes.back().first, iframes.back().second, cfgt, mt, cfgs, methods);
            iframes.pop_back();
        }
    }
};

void JitFrame::entry(bool cfgt, bool mt,
                     std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                     std::vector<std::pair<std::string, int>> &methods)
{
    const Method *mainm = _section->mainm();
    Block *block = mainm->get_bg()->offset2block(0);
    assert(block != nullptr);
    _iframes.push_back({mainm, block});
    if (cfgt) cfgs.push_back({mainm->id(), {block->get_begin_bci(), block->get_end_bci()}});
    if (mt) methods.push_back({"i", mainm->id()});
}

void JitFrame::clear()
{
    _iframes.clear();
}

void JitFrame::jit_code(std::vector<const PCStackInfo *> pcs,
                        bool cfgt, bool mt,
                        std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                        std::vector<std::pair<std::string, int>> &methods)
{
    /* Use SimNode to simulate execution of Jit */
    /* mark and then traverse */
    SimNode *node = new SimNode(_section->mainm());
    std::set<std::pair<const Method *, Block *>> totals;
    for (int i = 0; i < pcs.size(); ++i)
    {
        const PCStackInfo *pc = pcs[i];
        assert(pc);
        std::vector<std::pair<const Method *, Block *>> infos;

        for (int j = pc->numstackframes-1; j >= 0; --j)
        {
            const Method *m = _section->method(pc->methods[j]);
            /* m could be a non target method, use pc->bcis[j] to distinguish*/
            Block *block = (m && m->is_jportal()) ? m->get_bg()->offset2block(pc->bcis[j])
                             : (Block *)(uint64_t)pc->bcis[j];
            infos.push_back({m, block});
            totals.insert({m, block});
        }

        node->mark(infos, i, 0);

        /* iframe empty, set it to first bc */
        if (_iframes.empty())
        {
            _iframes = infos;
            for (auto iframe : _iframes)
            {
                if (iframe.first && iframe.first->is_jportal())
                {
                    if (cfgt)
                        cfgs.push_back({iframe.first->id(), {iframe.second->get_begin_bci(),
                                       iframe.second->get_end_bci()}});
                    if (mt)
                        methods.push_back({"i", iframe.first->id()});
                }
            }
        }
    }

    node->traverse(_iframes, 0, totals, cfgt, mt, cfgs, methods);
    delete node;
}

void JitFrame::jit_return(bool cfgt, bool mt,
                          std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                          std::vector<std::pair<std::string, int>> &methods)
{
    /* return */
    SimNode::return_iframes(_iframes, _iframes.size(), cfgt, mt, cfgs, methods);
}
