// Thornswood #4507. KillMoveFit, how getFittingKillMoves (KillMoveApi.cpp)
// combines an idle's conditions and orders the killmoves that fit.
#include "KillMoveFit.h"
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <vector>

struct Item
{
  bool holds = false;
  bool isOr = false;
  const Item* next = nullptr;
};

bool Groups(std::vector<Item>& items)
{
  for (size_t i = 0; i + 1 < items.size(); ++i) {
    items[i].next = &items[i + 1];
  }
  return KillMoveFit::GroupsHold(
    items.empty() ? nullptr : &items[0], [](const Item* i) { return i->next; },
    [](const Item* i) { return i->isOr; },
    [](const Item* i) { return i->holds; });
}

struct Node
{
  int id = 0;
  const Node* parent = nullptr;
  const Node* previous = nullptr;
  bool holds = true;
};

int main()
{
  { // (A or B) and C, as the game reads OR flags: each item's flag joins it
    // to the next
    std::vector<Item> v = { { false, true },
                            { true, false },
                            { true, false } };
    assert(Groups(v));
    v[1].holds = false;
    assert(!Groups(v));
    v[1].holds = true;
    v[2].holds = false;
    assert(!Groups(v));
  }
  { // No conditions hold; an OR on the last item still closes its group
    std::vector<Item> none;
    assert(Groups(none));
    std::vector<Item> v = { { false, true }, { false, true } };
    assert(!Groups(v));
    v[1].holds = true;
    assert(Groups(v));
  }
  { // Four weapon types ORed, as on the benthic lurker killmove: any one
    std::vector<Item> v = {
      { false, true }, { false, true }, { true, true }, { false, false }
    };
    assert(Groups(v));
  }

  auto parent = [](const Node* n) { return n->parent; };
  auto previous = [](const Node* n) { return n->previous; };

  // AttackRightRoot > NonMountedCombatRight > Root00 > { Wolf > {A, B}, Bear }
  Node top{ 1 }, combat{ 2, &top }, root{ 3, &combat };
  Node wolf{ 10, &root }, bear{ 20, &root, &wolf };
  Node wolfA{ 11, &wolf }, wolfB{ 12, &wolf, &wolfA };
  Node other{ 30, &combat, &root }; // a sibling of the root, not under it

  { // Depth first, siblings in their order, and a parent after its children
    std::vector<const Node*> nodes = { &bear, &wolfB, &wolf, &wolfA };
    std::sort(nodes.begin(), nodes.end(), [&](const Node* a, const Node* b) {
      return KillMoveFit::TreeOrderKey(a, parent, previous) <
        KillMoveFit::TreeOrderKey(b, parent, previous);
    });
    assert(nodes[0] == &wolfA);
    assert(nodes[1] == &wolfB);
    assert(nodes[2] == &wolf);
    assert(nodes[3] == &bear);
  }
  { // Under the root, and every node up to the top holds
    auto isRoot = [&](const Node* n) { return n == &root; };
    auto holdsHere = [](const Node* n) { return n->holds; };
    std::unordered_map<const Node*, bool> cache;
    assert(KillMoveFit::ChainHolds(&wolfA, parent, isRoot, holdsHere, cache));
    assert(!KillMoveFit::ChainHolds(&other, parent, isRoot, holdsHere, cache));
    assert(!KillMoveFit::ChainHolds(&top, parent, isRoot, holdsHere, cache));

    // A parent that fails takes its children with it, an ancestor above the
    // root too
    wolf.holds = false;
    cache.clear();
    assert(!KillMoveFit::ChainHolds(&wolfA, parent, isRoot, holdsHere, cache));
    assert(KillMoveFit::ChainHolds(&bear, parent, isRoot, holdsHere, cache));
    combat.holds = false;
    cache.clear();
    assert(!KillMoveFit::ChainHolds(&bear, parent, isRoot, holdsHere, cache));
    combat.holds = true;
    wolf.holds = true;

    // Each node is judged once a call
    int judged = 0;
    auto counting = [&](const Node* n) {
      ++judged;
      return n->holds;
    };
    cache.clear();
    KillMoveFit::ChainHolds(&wolfA, parent, isRoot, counting, cache);
    KillMoveFit::ChainHolds(&wolfB, parent, isRoot, counting, cache);
    assert(judged == 6); // wolfA, wolf, root, combat, top, then only wolfB
  }
  { // A loop in ANAM ends instead of hanging the game
    Node a{ 1 }, b{ 2, &a };
    a.parent = &b;
    auto isRoot = [&](const Node* n) { return n == &a; };
    std::unordered_map<const Node*, bool> cache;
    assert(!KillMoveFit::ChainHolds(
      &b, parent, isRoot, [](const Node*) { return true; }, cache));
    assert(KillMoveFit::TreeOrderKey(&b, parent, previous).size() ==
           static_cast<size_t>(KillMoveFit::kMaxDepth) + 1);
  }
  std::cout << "PASS KillMoveFit" << std::endl;
  return 0;
}
