#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

// THORNSWOOD PATCH (Thornswood #4507). How getFittingKillMoves
// (KillMoveApi.cpp) combines an idle's conditions and orders what fits,
// apart from the game's types so it is tested alone
// (misc/tests/node/test_kill_move_fit_native.cpp).
namespace KillMoveFit {

// An idle's conditions as the game combines them: items joined by OR make a
// group, an item that is not OR (or the last) closes it, and every group
// must hold. `next(item)` is the item after, `isOr(item)` its OR flag,
// `holds(item)` whether it holds. Evaluation stops inside a group once one
// item held, as the game's own does.
template <class Item, class Next, class IsOr, class Holds>
bool GroupsHold(const Item* head, Next next, IsOr isOr, Holds holds)
{
  bool group = false;
  for (auto item = head; item; item = next(item)) {
    if (!group) {
      group = holds(item);
    }
    if (!isOr(item) || !next(item)) {
      if (!group) {
        return false;
      }
      group = false;
    }
  }
  return true;
}

// Deeper than any chain in the masters (seven at most); a loop in a broken
// plugin's ANAM ends here.
constexpr int kMaxDepth = 32;

// The key that puts idles in the tree's order, depth first with siblings in
// their ANAM order: the sibling index of each node from the top down, then a
// last step after every child's, so a node that has children comes after
// them. `parent(node)` and `previous(node)` are its ANAM links.
template <class Node, class Parent, class Previous>
std::vector<int> TreeOrderKey(const Node* node, Parent parent,
                              Previous previous)
{
  std::vector<int> key;
  for (int depth = 0; node && depth < kMaxDepth;
       node = parent(node), ++depth) {
    int index = 0;
    for (auto p = previous(node); p && index < 4096; p = previous(p)) {
      ++index;
    }
    key.push_back(index);
  }
  std::reverse(key.begin(), key.end());
  key.push_back(std::numeric_limits<int>::max());
  return key;
}

// Whether `node` is under one of `roots` and its conditions and every
// parent's up to the top hold. `holdsHere(node)` is judged once a node,
// through `cache`.
template <class Node, class Parent, class IsRoot, class HoldsHere>
bool ChainHolds(const Node* node, Parent parent, IsRoot isRoot,
                HoldsHere holdsHere,
                std::unordered_map<const Node*, bool>& cache)
{
  std::vector<const Node*> chain;
  bool underRoot = false;
  for (auto n = node; n; n = parent(n)) {
    if (static_cast<int>(chain.size()) >= kMaxDepth) {
      return false;
    }
    chain.push_back(n);
    if (isRoot(n)) {
      underRoot = true;
    }
  }
  if (!underRoot) {
    return false;
  }
  for (auto n : chain) {
    auto it = cache.find(n);
    if (it == cache.end()) {
      it = cache.emplace(n, holdsHere(n)).first;
    }
    if (!it->second) {
      return false;
    }
  }
  return true;
}

}
