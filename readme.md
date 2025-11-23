# Data structure experiments for Mutable Value Semantics

## Flexible Array Members
- Should the layout differ based on where we allocate?
  - Todo prove that we cannot get enough space inside the extra padding that is introduced if we always allocate the space with alignment = max(alignof(Header), alignof(Element)) 

## Vectors
### Questions
- Should we ever shrink vectors?
- Should we have size and capacity inline or in the storage header?
- Should we have a small inline buffer for a couple of elements?

Implement basis operations, provide the rest through concept extensions

## Trees


## Maps
tree based vs flat structure tradeoffs

## Queues
circular vector vs linked list, tradeoffs