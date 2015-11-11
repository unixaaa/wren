#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "wren_common.h"
#include "wren_core.h"
#include "wren_primitive.h"
#include "wren_value.h"

#include "wren_core.wren.inc"

DEF_PRIMITIVE(bool_not)
{
  RETURN_BOOL(!AS_BOOL(args[0]));
}

DEF_PRIMITIVE(bool_toString)
{
  if (AS_BOOL(args[0]))
  {
    RETURN_VAL(CONST_STRING(vm, "true"));
  }
  else
  {
    RETURN_VAL(CONST_STRING(vm, "false"));
  }
}

DEF_PRIMITIVE(class_name)
{
  RETURN_OBJ(AS_CLASS(args[0])->name);
}

DEF_PRIMITIVE(class_supertype)
{
  ObjClass* classObj = AS_CLASS(args[0]);

  // Object has no superclass.
  if (classObj->superclass == NULL) RETURN_NULL;

  RETURN_OBJ(classObj->superclass);
}

DEF_PRIMITIVE(class_toString)
{
  RETURN_OBJ(AS_CLASS(args[0])->name);
}

DEF_PRIMITIVE(fiber_new)
{
  if (!validateFn(vm, args[1], "Argument")) return false;

  ObjFiber* newFiber = wrenNewFiber(vm, AS_OBJ(args[1]));

  // The compiler expects the first slot of a function to hold the receiver.
  // Since a fiber's stack is invoked directly, it doesn't have one, so put it
  // in here.
  // TODO: Is there a cleaner solution?
  // TODO: If we make growable stacks, make sure this grows it.
  newFiber->stack[0] = NULL_VAL;
  newFiber->stackTop++;

  RETURN_OBJ(newFiber);
}

DEF_PRIMITIVE(fiber_abort)
{
  vm->fiber->error = args[1];

  // If the error is explicitly null, it's not really an abort.
  return IS_NULL(args[1]);
}

// Transfer execution to [fiber] coming from the current fiber whose stack has
// [args].
//
// [isCall] is true if [fiber] is being called and not transferred.
//
// [hasValue] is true if a value in [args] is being passed to the new fiber.
// Otherwise, `null` is implicitly being passed.
static bool runFiber(WrenVM* vm, ObjFiber* fiber, Value* args, bool isCall,
                     bool hasValue)
{
  if (isCall)
  {
    if (fiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

    // Remember who ran it.
    fiber->caller = vm->fiber;
  }

  if (fiber->numFrames == 0)
  {
    vm->fiber->error = wrenStringFormat(vm, "Cannot $ a finished fiber.",
                               isCall ? "call" : "transfer to");
    return false;
  }

  if (!IS_NULL(fiber->error))
  {
    vm->fiber->error = wrenStringFormat(vm, "Cannot $ an aborted fiber.",
                               isCall ? "call" : "transfer to");
    return false;
  }

  // When the calling fiber resumes, we'll store the result of the call in its
  // stack. If the call has two arguments (the fiber and the value), we only
  // need one slot for the result, so discard the other slot now.
  if (hasValue) vm->fiber->stackTop--;

  // If the fiber was paused, make yield() or transfer() return the result.
  if (fiber->stackTop > fiber->stack)
  {
    fiber->stackTop[-1] = hasValue ? args[1] : NULL_VAL;
  }

  vm->fiber = fiber;
  return false;
}

DEF_PRIMITIVE(fiber_call)
{
  return runFiber(vm, AS_FIBER(args[0]), args, true, false);
}

DEF_PRIMITIVE(fiber_call1)
{
  return runFiber(vm, AS_FIBER(args[0]), args, true, true);
}

DEF_PRIMITIVE(fiber_current)
{
  RETURN_OBJ(vm->fiber);
}

DEF_PRIMITIVE(fiber_error)
{
  RETURN_VAL(AS_FIBER(args[0])->error);
}

DEF_PRIMITIVE(fiber_isDone)
{
  ObjFiber* runFiber = AS_FIBER(args[0]);
  RETURN_BOOL(runFiber->numFrames == 0 || !IS_NULL(runFiber->error));
}

DEF_PRIMITIVE(fiber_suspend)
{
  // Switching to a null fiber tells the interpreter to stop and exit.
  vm->fiber = NULL;
  return false;
}

DEF_PRIMITIVE(fiber_transfer)
{
  return runFiber(vm, AS_FIBER(args[0]), args, false, false);
}

DEF_PRIMITIVE(fiber_transfer1)
{
  return runFiber(vm, AS_FIBER(args[0]), args, false, true);
}

DEF_PRIMITIVE(fiber_transferError)
{
  runFiber(vm, AS_FIBER(args[0]), args, false, true);
  vm->fiber->error = args[1];
  return false;
}

DEF_PRIMITIVE(fiber_try)
{
  ObjFiber* current = vm->fiber;
  ObjFiber* tried = AS_FIBER(args[0]);
  // TODO: Use runFiber().
  if (tried->numFrames == 0) RETURN_ERROR("Cannot try a finished fiber.");
  if (tried->caller != NULL) RETURN_ERROR("Fiber has already been called.");

  vm->fiber = tried;

  // Remember who ran it.
  vm->fiber->caller = current;
  vm->fiber->callerIsTrying = true;

  // If the fiber was yielded, make the yield call return null.
  if (vm->fiber->stackTop > vm->fiber->stack)
  {
    vm->fiber->stackTop[-1] = NULL_VAL;
  }

  return false;
}

DEF_PRIMITIVE(fiber_yield)
{
  ObjFiber* current = vm->fiber;
  vm->fiber = current->caller;

  // Unhook this fiber from the one that called it.
  current->caller = NULL;
  current->callerIsTrying = false;

  if (vm->fiber != NULL)
  {
    // Make the caller's run method return null.
    vm->fiber->stackTop[-1] = NULL_VAL;
  }

  return false;
}

DEF_PRIMITIVE(fiber_yield1)
{
  ObjFiber* current = vm->fiber;
  vm->fiber = current->caller;

  // Unhook this fiber from the one that called it.
  current->caller = NULL;
  current->callerIsTrying = false;

  if (vm->fiber != NULL)
  {
    // Make the caller's run method return the argument passed to yield.
    vm->fiber->stackTop[-1] = args[1];

    // When the yielding fiber resumes, we'll store the result of the yield
    // call in its stack. Since Fiber.yield(value) has two arguments (the Fiber
    // class and the value) and we only need one slot for the result, discard
    // the other slot now.
    current->stackTop--;
  }

  return false;
}

DEF_PRIMITIVE(fn_new)
{
  if (!validateFn(vm, args[1], "Argument")) return false;

  // The block argument is already a function, so just return it.
  RETURN_VAL(args[1]);
}

DEF_PRIMITIVE(fn_arity)
{
  RETURN_NUM(AS_FN(args[0])->arity);
}

DEF_PRIMITIVE(fn_toString)
{
  RETURN_VAL(CONST_STRING(vm, "<fn>"));
}

DEF_PRIMITIVE(list_new)
{
  RETURN_OBJ(wrenNewList(vm, 0));
}

DEF_PRIMITIVE(list_add)
{
  wrenValueBufferWrite(vm, &AS_LIST(args[0])->elements, args[1]);
  RETURN_VAL(args[1]);
}

// Adds an element to the list and then returns the list itself. This is called
// by the compiler when compiling list literals instead of using add() to
// minimize stack churn.
DEF_PRIMITIVE(list_addCore)
{
  wrenValueBufferWrite(vm, &AS_LIST(args[0])->elements, args[1]);
  
  // Return the list.
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(list_clear)
{
  wrenValueBufferClear(vm, &AS_LIST(args[0])->elements);
  RETURN_NULL;
}

DEF_PRIMITIVE(list_count)
{
  RETURN_NUM(AS_LIST(args[0])->elements.count);
}

DEF_PRIMITIVE(list_insert)
{
  ObjList* list = AS_LIST(args[0]);

  // count + 1 here so you can "insert" at the very end.
  uint32_t index = validateIndex(vm, args[1], list->elements.count + 1,
                                 "Index");
  if (index == UINT32_MAX) return false;

  wrenListInsert(vm, list, args[2], index);
  RETURN_VAL(args[2]);
}

DEF_PRIMITIVE(list_iterate)
{
  ObjList* list = AS_LIST(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (list->elements.count == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  // Stop if we're out of bounds.
  double index = AS_NUM(args[1]);
  if (index < 0 || index >= list->elements.count - 1) RETURN_FALSE;

  // Otherwise, move to the next index.
  RETURN_NUM(index + 1);
}

DEF_PRIMITIVE(list_iteratorValue)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count, "Iterator");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(list->elements.data[index]);
}

DEF_PRIMITIVE(list_removeAt)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count, "Index");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(wrenListRemoveAt(vm, list, index));
}

DEF_PRIMITIVE(list_subscript)
{
  ObjList* list = AS_LIST(args[0]);

  if (IS_NUM(args[1]))
  {
    uint32_t index = validateIndex(vm, args[1], list->elements.count,
                                   "Subscript");
    if (index == UINT32_MAX) return false;

    RETURN_VAL(list->elements.data[index]);
  }

  if (!IS_RANGE(args[1]))
  {
    RETURN_ERROR("Subscript must be a number or a range.");
  }

  int step;
  uint32_t count = list->elements.count;
  uint32_t start = calculateRange(vm, AS_RANGE(args[1]), &count, &step);
  if (start == UINT32_MAX) return false;

  ObjList* result = wrenNewList(vm, count);
  for (uint32_t i = 0; i < count; i++)
  {
    result->elements.data[i] = list->elements.data[start + i * step];
  }

  RETURN_OBJ(result);
}

DEF_PRIMITIVE(list_subscriptSetter)
{
  ObjList* list = AS_LIST(args[0]);
  uint32_t index = validateIndex(vm, args[1], list->elements.count,
                                 "Subscript");
  if (index == UINT32_MAX) return false;

  list->elements.data[index] = args[2];
  RETURN_VAL(args[2]);
}

DEF_PRIMITIVE(map_new)
{
  RETURN_OBJ(wrenNewMap(vm));
}

DEF_PRIMITIVE(map_subscript)
{
  if (!validateKey(vm, args[1])) return false;

  ObjMap* map = AS_MAP(args[0]);
  Value value = wrenMapGet(map, args[1]);
  if (IS_UNDEFINED(value)) RETURN_NULL;

  RETURN_VAL(value);
}

DEF_PRIMITIVE(map_subscriptSetter)
{
  if (!validateKey(vm, args[1])) return false;

  wrenMapSet(vm, AS_MAP(args[0]), args[1], args[2]);
  RETURN_VAL(args[2]);
}

// Adds an entry to the map and then returns the map itself. This is called by
// the compiler when compiling map literals instead of using [_]=(_) to
// minimize stack churn.
DEF_PRIMITIVE(map_addCore)
{
  if (!validateKey(vm, args[1])) return false;
  
  wrenMapSet(vm, AS_MAP(args[0]), args[1], args[2]);
  
  // Return the map itself.
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(map_clear)
{
  wrenMapClear(vm, AS_MAP(args[0]));
  RETURN_NULL;
}

DEF_PRIMITIVE(map_containsKey)
{
  if (!validateKey(vm, args[1])) return false;

  RETURN_BOOL(!IS_UNDEFINED(wrenMapGet(AS_MAP(args[0]), args[1])));
}

DEF_PRIMITIVE(map_count)
{
  RETURN_NUM(AS_MAP(args[0])->count);
}

DEF_PRIMITIVE(map_iterate)
{
  ObjMap* map = AS_MAP(args[0]);

  if (map->count == 0) RETURN_FALSE;

  // If we're starting the iteration, start at the first used entry.
  uint32_t index = 0;

  // Otherwise, start one past the last entry we stopped at.
  if (!IS_NULL(args[1]))
  {
    if (!validateInt(vm, args[1], "Iterator")) return false;

    if (AS_NUM(args[1]) < 0) RETURN_FALSE;
    index = (uint32_t)AS_NUM(args[1]);

    if (index >= map->capacity) RETURN_FALSE;

    // Advance the iterator.
    index++;
  }

  // Find a used entry, if any.
  for (; index < map->capacity; index++)
  {
    if (!IS_UNDEFINED(map->entries[index].key)) RETURN_NUM(index);
  }

  // If we get here, walked all of the entries.
  RETURN_FALSE;
}

DEF_PRIMITIVE(map_remove)
{
  if (!validateKey(vm, args[1])) return false;

  RETURN_VAL(wrenMapRemoveKey(vm, AS_MAP(args[0]), args[1]));
}

DEF_PRIMITIVE(map_keyIteratorValue)
{
  ObjMap* map = AS_MAP(args[0]);
  uint32_t index = validateIndex(vm, args[1], map->capacity, "Iterator");
  if (index == UINT32_MAX) return false;

  MapEntry* entry = &map->entries[index];
  if (IS_UNDEFINED(entry->key))
  {
    RETURN_ERROR("Invalid map iterator value.");
  }

  RETURN_VAL(entry->key);
}

DEF_PRIMITIVE(map_valueIteratorValue)
{
  ObjMap* map = AS_MAP(args[0]);
  uint32_t index = validateIndex(vm, args[1], map->capacity, "Iterator");
  if (index == UINT32_MAX) return false;

  MapEntry* entry = &map->entries[index];
  if (IS_UNDEFINED(entry->key))
  {
    RETURN_ERROR("Invalid map iterator value.");
  }

  RETURN_VAL(entry->value);
}

DEF_PRIMITIVE(null_not)
{
  RETURN_VAL(TRUE_VAL);
}

DEF_PRIMITIVE(null_toString)
{
  RETURN_VAL(CONST_STRING(vm, "null"));
}

DEF_PRIMITIVE(num_fromString)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[1]);

  // Corner case: Can't parse an empty string.
  if (string->length == 0) RETURN_NULL;

  errno = 0;
  char* end;
  double number = strtod(string->value, &end);

  // Skip past any trailing whitespace.
  while (*end != '\0' && isspace((unsigned char)*end)) end++;

  if (errno == ERANGE)
  {
    vm->fiber->error = CONST_STRING(vm, "Number literal is too large.");
    return false;
  }

  // We must have consumed the entire string. Otherwise, it contains non-number
  // characters and we can't parse it.
  if (end < string->value + string->length) RETURN_NULL;

  RETURN_NUM(number);
}

DEF_PRIMITIVE(num_pi)
{
  RETURN_NUM(3.14159265358979323846);
}

// Defines a primitive on Num that calls infix [op] and returns [type].
#define DEF_NUM_INFIX(name, op, type) \
    DEF_PRIMITIVE(num_##name) \
    { \
      if (!validateNum(vm, args[1], "Right operand")) return false; \
      RETURN_##type(AS_NUM(args[0]) op AS_NUM(args[1])); \
    }

DEF_NUM_INFIX(minus,    -,  NUM)
DEF_NUM_INFIX(plus,     +,  NUM)
DEF_NUM_INFIX(multiply, *,  NUM)
DEF_NUM_INFIX(divide,   /,  NUM)
DEF_NUM_INFIX(lt,       <,  BOOL)
DEF_NUM_INFIX(gt,       >,  BOOL)
DEF_NUM_INFIX(lte,      <=, BOOL)
DEF_NUM_INFIX(gte,      >=, BOOL)

// Defines a primitive on Num that call infix bitwise [op].
#define DEF_NUM_BITWISE(name, op) \
    DEF_PRIMITIVE(num_bitwise##name) \
    { \
      if (!validateNum(vm, args[1], "Right operand")) return false; \
      uint32_t left = (uint32_t)AS_NUM(args[0]); \
      uint32_t right = (uint32_t)AS_NUM(args[1]); \
      RETURN_NUM(left op right); \
    }

DEF_NUM_BITWISE(And,        &)
DEF_NUM_BITWISE(Or,         |)
DEF_NUM_BITWISE(Xor,        ^)
DEF_NUM_BITWISE(LeftShift,  <<)
DEF_NUM_BITWISE(RightShift, >>)

// Defines a primitive method on Num that returns the result of [fn].
#define DEF_NUM_FN(name, fn) \
    DEF_PRIMITIVE(num_##name) \
    { \
      RETURN_NUM(fn(AS_NUM(args[0]))); \
    }

DEF_NUM_FN(abs,     fabs)
DEF_NUM_FN(acos,    acos)
DEF_NUM_FN(asin,    asin)
DEF_NUM_FN(atan,    atan)
DEF_NUM_FN(ceil,    ceil)
DEF_NUM_FN(cos,     cos)
DEF_NUM_FN(floor,   floor)
DEF_NUM_FN(negate,  -)
DEF_NUM_FN(sin,     sin)
DEF_NUM_FN(sqrt,    sqrt)
DEF_NUM_FN(tan,     tan)

DEF_PRIMITIVE(num_mod)
{
  if (!validateNum(vm, args[1], "Right operand")) return false;
  RETURN_NUM(fmod(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_PRIMITIVE(num_eqeq)
{
  if (!IS_NUM(args[1])) RETURN_FALSE;
  RETURN_BOOL(AS_NUM(args[0]) == AS_NUM(args[1]));
}

DEF_PRIMITIVE(num_bangeq)
{
  if (!IS_NUM(args[1])) RETURN_TRUE;
  RETURN_BOOL(AS_NUM(args[0]) != AS_NUM(args[1]));
}

DEF_PRIMITIVE(num_bitwiseNot)
{
  // Bitwise operators always work on 32-bit unsigned ints.
  RETURN_NUM(~(uint32_t)AS_NUM(args[0]));
}

DEF_PRIMITIVE(num_dotDot)
{
  if (!validateNum(vm, args[1], "Right hand side of range")) return false;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);
  RETURN_VAL(wrenNewRange(vm, from, to, true));
}

DEF_PRIMITIVE(num_dotDotDot)
{
  if (!validateNum(vm, args[1], "Right hand side of range")) return false;

  double from = AS_NUM(args[0]);
  double to = AS_NUM(args[1]);
  RETURN_VAL(wrenNewRange(vm, from, to, false));
}

DEF_PRIMITIVE(num_atan2)
{
  RETURN_NUM(atan2(AS_NUM(args[0]), AS_NUM(args[1])));
}

DEF_PRIMITIVE(num_fraction)
{
  double dummy;
  RETURN_NUM(modf(AS_NUM(args[0]) , &dummy));
}

DEF_PRIMITIVE(num_isInfinity)
{
  RETURN_BOOL(isinf(AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_isInteger)
{
  double value = AS_NUM(args[0]);
  if (isnan(value) || isinf(value)) RETURN_FALSE;
  RETURN_BOOL(trunc(value) == value);
}

DEF_PRIMITIVE(num_isNan)
{
  RETURN_BOOL(isnan(AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_sign)
{
  double value = AS_NUM(args[0]);
  if (value > 0)
  {
    RETURN_NUM(1);
  }
  else if (value < 0)
  {
    RETURN_NUM(-1);
  }
  else
  {
    RETURN_NUM(0);
  }
}

DEF_PRIMITIVE(num_toString)
{
  RETURN_VAL(wrenNumToString(vm, AS_NUM(args[0])));
}

DEF_PRIMITIVE(num_truncate)
{
  double integer;
  modf(AS_NUM(args[0]) , &integer);
  RETURN_NUM(integer);
}

DEF_PRIMITIVE(object_same)
{
  RETURN_BOOL(wrenValuesEqual(args[1], args[2]));
}

DEF_PRIMITIVE(object_not)
{
  RETURN_VAL(FALSE_VAL);
}

DEF_PRIMITIVE(object_eqeq)
{
  RETURN_BOOL(wrenValuesEqual(args[0], args[1]));
}

DEF_PRIMITIVE(object_bangeq)
{
  RETURN_BOOL(!wrenValuesEqual(args[0], args[1]));
}

DEF_PRIMITIVE(object_is)
{
  if (!IS_CLASS(args[1]))
  {
    RETURN_ERROR("Right operand must be a class.");
  }

  ObjClass *classObj = wrenGetClass(vm, args[0]);
  ObjClass *baseClassObj = AS_CLASS(args[1]);

  // Walk the superclass chain looking for the class.
  do
  {
    if (baseClassObj == classObj) RETURN_BOOL(true);

    classObj = classObj->superclass;
  }
  while (classObj != NULL);

  RETURN_BOOL(false);
}

DEF_PRIMITIVE(object_toString)
{
  Obj* obj = AS_OBJ(args[0]);
  Value name = OBJ_VAL(obj->classObj->name);
  RETURN_VAL(wrenStringFormat(vm, "instance of @", name));
}

DEF_PRIMITIVE(object_type)
{
  RETURN_OBJ(wrenGetClass(vm, args[0]));
}

DEF_PRIMITIVE(range_from)
{
  RETURN_NUM(AS_RANGE(args[0])->from);
}

DEF_PRIMITIVE(range_to)
{
  RETURN_NUM(AS_RANGE(args[0])->to);
}

DEF_PRIMITIVE(range_min)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmin(range->from, range->to));
}

DEF_PRIMITIVE(range_max)
{
  ObjRange* range = AS_RANGE(args[0]);
  RETURN_NUM(fmax(range->from, range->to));
}

DEF_PRIMITIVE(range_isInclusive)
{
  RETURN_BOOL(AS_RANGE(args[0])->isInclusive);
}

DEF_PRIMITIVE(range_iterate)
{
  ObjRange* range = AS_RANGE(args[0]);

  // Special case: empty range.
  if (range->from == range->to && !range->isInclusive) RETURN_FALSE;

  // Start the iteration.
  if (IS_NULL(args[1])) RETURN_NUM(range->from);

  if (!validateNum(vm, args[1], "Iterator")) return false;

  double iterator = AS_NUM(args[1]);

  // Iterate towards [to] from [from].
  if (range->from < range->to)
  {
    iterator++;
    if (iterator > range->to) RETURN_FALSE;
  }
  else
  {
    iterator--;
    if (iterator < range->to) RETURN_FALSE;
  }

  if (!range->isInclusive && iterator == range->to) RETURN_FALSE;

  RETURN_NUM(iterator);
}

DEF_PRIMITIVE(range_iteratorValue)
{
  // Assume the iterator is a number so that is the value of the range.
  RETURN_VAL(args[1]);
}

DEF_PRIMITIVE(range_toString)
{
  ObjRange* range = AS_RANGE(args[0]);

  Value from = wrenNumToString(vm, range->from);
  wrenPushRoot(vm, AS_OBJ(from));

  Value to = wrenNumToString(vm, range->to);
  wrenPushRoot(vm, AS_OBJ(to));

  Value result = wrenStringFormat(vm, "@$@", from,
                                  range->isInclusive ? ".." : "...", to);

  wrenPopRoot(vm);
  wrenPopRoot(vm);
  RETURN_VAL(result);
}

DEF_PRIMITIVE(string_fromCodePoint)
{
  if (!validateInt(vm, args[1], "Code point")) return false;

  int codePoint = (int)AS_NUM(args[1]);
  if (codePoint < 0)
  {
    RETURN_ERROR("Code point cannot be negative.");
  }
  else if (codePoint > 0x10ffff)
  {
    RETURN_ERROR("Code point cannot be greater than 0x10ffff.");
  }

  RETURN_VAL(wrenStringFromCodePoint(vm, codePoint));
}

DEF_PRIMITIVE(string_byteAt)
{
  ObjString* string = AS_STRING(args[0]);

  uint32_t index = validateIndex(vm, args[1], string->length, "Index");
  if (index == UINT32_MAX) return false;

  RETURN_NUM((uint8_t)string->value[index]);
}

DEF_PRIMITIVE(string_byteCount)
{
  RETURN_NUM(AS_STRING(args[0])->length);
}

DEF_PRIMITIVE(string_codePointAt)
{
  ObjString* string = AS_STRING(args[0]);

  uint32_t index = validateIndex(vm, args[1], string->length, "Index");
  if (index == UINT32_MAX) return false;

  // If we are in the middle of a UTF-8 sequence, indicate that.
  const uint8_t* bytes = (uint8_t*)string->value;
  if ((bytes[index] & 0xc0) == 0x80) RETURN_NUM(-1);

  // Decode the UTF-8 sequence.
  RETURN_NUM(wrenUtf8Decode((uint8_t*)string->value + index,
                            string->length - index));
}

DEF_PRIMITIVE(string_contains)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  RETURN_BOOL(wrenStringFind(string, search) != UINT32_MAX);
}

DEF_PRIMITIVE(string_endsWith)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Corner case, if the search string is longer than return false right away.
  if (search->length > string->length) RETURN_FALSE;

  RETURN_BOOL(memcmp(string->value + string->length - search->length,
                     search->value, search->length) == 0);
}

DEF_PRIMITIVE(string_indexOf)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  uint32_t index = wrenStringFind(string, search);
  RETURN_NUM(index == UINT32_MAX ? -1 : (int)index);
}

DEF_PRIMITIVE(string_iterate)
{
  ObjString* string = AS_STRING(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (string->length == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  if (AS_NUM(args[1]) < 0) RETURN_FALSE;
  uint32_t index = (uint32_t)AS_NUM(args[1]);

  // Advance to the beginning of the next UTF-8 sequence.
  do
  {
    index++;
    if (index >= string->length) RETURN_FALSE;
  } while ((string->value[index] & 0xc0) == 0x80);

  RETURN_NUM(index);
}

DEF_PRIMITIVE(string_iterateByte)
{
  ObjString* string = AS_STRING(args[0]);

  // If we're starting the iteration, return the first index.
  if (IS_NULL(args[1]))
  {
    if (string->length == 0) RETURN_FALSE;
    RETURN_NUM(0);
  }

  if (!validateInt(vm, args[1], "Iterator")) return false;

  if (AS_NUM(args[1]) < 0) RETURN_FALSE;
  uint32_t index = (uint32_t)AS_NUM(args[1]);

  // Advance to the next byte.
  index++;
  if (index >= string->length) RETURN_FALSE;

  RETURN_NUM(index);
}

DEF_PRIMITIVE(string_iteratorValue)
{
  ObjString* string = AS_STRING(args[0]);
  uint32_t index = validateIndex(vm, args[1], string->length, "Iterator");
  if (index == UINT32_MAX) return false;

  RETURN_VAL(wrenStringCodePointAt(vm, string, index));
}

DEF_PRIMITIVE(string_startsWith)
{
  if (!validateString(vm, args[1], "Argument")) return false;

  ObjString* string = AS_STRING(args[0]);
  ObjString* search = AS_STRING(args[1]);

  // Corner case, if the search string is longer than return false right away.
  if (search->length > string->length) RETURN_FALSE;

  RETURN_BOOL(memcmp(string->value, search->value, search->length) == 0);
}

DEF_PRIMITIVE(string_plus)
{
  if (!validateString(vm, args[1], "Right operand")) return false;
  RETURN_VAL(wrenStringFormat(vm, "@@", args[0], args[1]));
}

DEF_PRIMITIVE(string_subscript)
{
  ObjString* string = AS_STRING(args[0]);

  if (IS_NUM(args[1]))
  {
    int index = validateIndex(vm, args[1], string->length, "Subscript");
    if (index == -1) return false;

    RETURN_VAL(wrenStringCodePointAt(vm, string, index));
  }

  if (!IS_RANGE(args[1]))
  {
    RETURN_ERROR("Subscript must be a number or a range.");
  }

  int step;
  uint32_t count = string->length;
  int start = calculateRange(vm, AS_RANGE(args[1]), &count, &step);
  if (start == -1) return false;

  RETURN_VAL(wrenNewStringFromRange(vm, string, start, count, step));
}

DEF_PRIMITIVE(string_toString)
{
  RETURN_VAL(args[0]);
}

DEF_PRIMITIVE(system_clock)
{
  RETURN_NUM((double)clock() / CLOCKS_PER_SEC);
}

DEF_PRIMITIVE(system_gc)
{
  wrenCollectGarbage(vm);
  RETURN_NULL;
}

DEF_PRIMITIVE(system_writeString)
{
  if (vm->config.writeFn != NULL)
  {
    vm->config.writeFn(vm, AS_CSTRING(args[1]));
  }

  RETURN_VAL(args[1]);
}

// Creates either the Object or Class class in the core module with [name].
static ObjClass* defineClass(WrenVM* vm, ObjModule* module, const char* name)
{
  ObjString* nameString = AS_STRING(wrenStringFormat(vm, "$", name));
  wrenPushRoot(vm, (Obj*)nameString);

  ObjClass* classObj = wrenNewSingleClass(vm, 0, nameString);

  wrenDefineVariable(vm, module, name, nameString->length, OBJ_VAL(classObj));

  wrenPopRoot(vm);
  return classObj;
}

// Defines one of the overloads of the special "call(...)" method on Fn.
//
// These methods have their own unique method type to handle pushing the
// function onto the callstack and checking its arity.
static void fnCall(WrenVM* vm, const char* signature)
{
  int symbol = wrenSymbolTableEnsure(vm, &vm->methodNames, signature,
                                     strlen(signature));
  Method method;
  method.type = METHOD_FN_CALL;
  wrenBindMethod(vm, vm->fnClass, symbol, method);
}

void wrenInitializeCore(WrenVM* vm)
{
  ObjModule* coreModule = wrenNewModule(vm, NULL);
  wrenPushRoot(vm, (Obj*)coreModule);
  
  // The core module's key is null in the module map.
  wrenMapSet(vm, vm->modules, NULL_VAL, OBJ_VAL(coreModule));
  wrenPopRoot(vm); // coreModule.

  // Define the root Object class. This has to be done a little specially
  // because it has no superclass.
  vm->objectClass = defineClass(vm, coreModule, "Object");
  PRIMITIVE(vm->objectClass, "!", object_not);
  PRIMITIVE(vm->objectClass, "==(_)", object_eqeq);
  PRIMITIVE(vm->objectClass, "!=(_)", object_bangeq);
  PRIMITIVE(vm->objectClass, "is(_)", object_is);
  PRIMITIVE(vm->objectClass, "toString", object_toString);
  PRIMITIVE(vm->objectClass, "type", object_type);

  // Now we can define Class, which is a subclass of Object.
  vm->classClass = defineClass(vm, coreModule, "Class");
  wrenBindSuperclass(vm, vm->classClass, vm->objectClass);
  PRIMITIVE(vm->classClass, "name", class_name);
  PRIMITIVE(vm->classClass, "supertype", class_supertype);
  PRIMITIVE(vm->classClass, "toString", class_toString);

  // Finally, we can define Object's metaclass which is a subclass of Class.
  ObjClass* objectMetaclass = defineClass(vm, coreModule, "Object metaclass");

  // Wire up the metaclass relationships now that all three classes are built.
  vm->objectClass->obj.classObj = objectMetaclass;
  objectMetaclass->obj.classObj = vm->classClass;
  vm->classClass->obj.classObj = vm->classClass;

  // Do this after wiring up the metaclasses so objectMetaclass doesn't get
  // collected.
  wrenBindSuperclass(vm, objectMetaclass, vm->classClass);

  PRIMITIVE(objectMetaclass, "same(_,_)", object_same);

  // The core class diagram ends up looking like this, where single lines point
  // to a class's superclass, and double lines point to its metaclass:
  //
  //        .------------------------------------. .====.
  //        |                  .---------------. | #    #
  //        v                  |               v | v    #
  //   .---------.   .-------------------.   .-------.  #
  //   | Object  |==>| Object metaclass  |==>| Class |=="
  //   '---------'   '-------------------'   '-------'
  //        ^                                 ^ ^ ^ ^
  //        |                  .--------------' # | #
  //        |                  |                # | #
  //   .---------.   .-------------------.      # | # -.
  //   |  Base   |==>|  Base metaclass   |======" | #  |
  //   '---------'   '-------------------'        | #  |
  //        ^                                     | #  |
  //        |                  .------------------' #  | Example classes
  //        |                  |                    #  |
  //   .---------.   .-------------------.          #  |
  //   | Derived |==>| Derived metaclass |=========="  |
  //   '---------'   '-------------------'            -'

  // The rest of the classes can now be defined normally.
  wrenInterpretInModule(vm, NULL, coreModuleSource);

  vm->boolClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Bool"));
  PRIMITIVE(vm->boolClass, "toString", bool_toString);
  PRIMITIVE(vm->boolClass, "!", bool_not);

  vm->fiberClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Fiber"));
  PRIMITIVE(vm->fiberClass->obj.classObj, "new(_)", fiber_new);
  PRIMITIVE(vm->fiberClass->obj.classObj, "abort(_)", fiber_abort);
  PRIMITIVE(vm->fiberClass->obj.classObj, "current", fiber_current);
  PRIMITIVE(vm->fiberClass->obj.classObj, "suspend()", fiber_suspend);
  PRIMITIVE(vm->fiberClass->obj.classObj, "yield()", fiber_yield);
  PRIMITIVE(vm->fiberClass->obj.classObj, "yield(_)", fiber_yield1);
  PRIMITIVE(vm->fiberClass, "call()", fiber_call);
  PRIMITIVE(vm->fiberClass, "call(_)", fiber_call1);
  PRIMITIVE(vm->fiberClass, "error", fiber_error);
  PRIMITIVE(vm->fiberClass, "isDone", fiber_isDone);
  PRIMITIVE(vm->fiberClass, "transfer()", fiber_transfer);
  PRIMITIVE(vm->fiberClass, "transfer(_)", fiber_transfer1);
  PRIMITIVE(vm->fiberClass, "transferError(_)", fiber_transferError);
  PRIMITIVE(vm->fiberClass, "try()", fiber_try);

  vm->fnClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Fn"));
  PRIMITIVE(vm->fnClass->obj.classObj, "new(_)", fn_new);

  PRIMITIVE(vm->fnClass, "arity", fn_arity);
  fnCall(vm, "call()");
  fnCall(vm, "call(_)");
  fnCall(vm, "call(_,_)");
  fnCall(vm, "call(_,_,_)");
  fnCall(vm, "call(_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
  fnCall(vm, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)");
  PRIMITIVE(vm->fnClass, "toString", fn_toString);

  vm->nullClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Null"));
  PRIMITIVE(vm->nullClass, "!", null_not);
  PRIMITIVE(vm->nullClass, "toString", null_toString);

  vm->numClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Num"));
  PRIMITIVE(vm->numClass->obj.classObj, "fromString(_)", num_fromString);
  PRIMITIVE(vm->numClass->obj.classObj, "pi", num_pi);
  PRIMITIVE(vm->numClass, "-(_)", num_minus);
  PRIMITIVE(vm->numClass, "+(_)", num_plus);
  PRIMITIVE(vm->numClass, "*(_)", num_multiply);
  PRIMITIVE(vm->numClass, "/(_)", num_divide);
  PRIMITIVE(vm->numClass, "<(_)", num_lt);
  PRIMITIVE(vm->numClass, ">(_)", num_gt);
  PRIMITIVE(vm->numClass, "<=(_)", num_lte);
  PRIMITIVE(vm->numClass, ">=(_)", num_gte);
  PRIMITIVE(vm->numClass, "&(_)", num_bitwiseAnd);
  PRIMITIVE(vm->numClass, "|(_)", num_bitwiseOr);
  PRIMITIVE(vm->numClass, "^(_)", num_bitwiseXor);
  PRIMITIVE(vm->numClass, "<<(_)", num_bitwiseLeftShift);
  PRIMITIVE(vm->numClass, ">>(_)", num_bitwiseRightShift);
  PRIMITIVE(vm->numClass, "abs", num_abs);
  PRIMITIVE(vm->numClass, "acos", num_acos);
  PRIMITIVE(vm->numClass, "asin", num_asin);
  PRIMITIVE(vm->numClass, "atan", num_atan);
  PRIMITIVE(vm->numClass, "ceil", num_ceil);
  PRIMITIVE(vm->numClass, "cos", num_cos);
  PRIMITIVE(vm->numClass, "floor", num_floor);
  PRIMITIVE(vm->numClass, "-", num_negate);
  PRIMITIVE(vm->numClass, "sin", num_sin);
  PRIMITIVE(vm->numClass, "sqrt", num_sqrt);
  PRIMITIVE(vm->numClass, "tan", num_tan);
  PRIMITIVE(vm->numClass, "%(_)", num_mod);
  PRIMITIVE(vm->numClass, "~", num_bitwiseNot);
  PRIMITIVE(vm->numClass, "..(_)", num_dotDot);
  PRIMITIVE(vm->numClass, "...(_)", num_dotDotDot);
  PRIMITIVE(vm->numClass, "atan(_)", num_atan2);
  PRIMITIVE(vm->numClass, "fraction", num_fraction);
  PRIMITIVE(vm->numClass, "isInfinity", num_isInfinity);
  PRIMITIVE(vm->numClass, "isInteger", num_isInteger);
  PRIMITIVE(vm->numClass, "isNan", num_isNan);
  PRIMITIVE(vm->numClass, "sign", num_sign);
  PRIMITIVE(vm->numClass, "toString", num_toString);
  PRIMITIVE(vm->numClass, "truncate", num_truncate);

  // These are defined just so that 0 and -0 are equal, which is specified by
  // IEEE 754 even though they have different bit representations.
  PRIMITIVE(vm->numClass, "==(_)", num_eqeq);
  PRIMITIVE(vm->numClass, "!=(_)", num_bangeq);

  vm->stringClass = AS_CLASS(wrenFindVariable(vm, coreModule, "String"));
  PRIMITIVE(vm->stringClass->obj.classObj, "fromCodePoint(_)", string_fromCodePoint);
  PRIMITIVE(vm->stringClass, "+(_)", string_plus);
  PRIMITIVE(vm->stringClass, "[_]", string_subscript);
  PRIMITIVE(vm->stringClass, "byteAt_(_)", string_byteAt);
  PRIMITIVE(vm->stringClass, "byteCount_", string_byteCount);
  PRIMITIVE(vm->stringClass, "codePointAt_(_)", string_codePointAt);
  PRIMITIVE(vm->stringClass, "contains(_)", string_contains);
  PRIMITIVE(vm->stringClass, "endsWith(_)", string_endsWith);
  PRIMITIVE(vm->stringClass, "indexOf(_)", string_indexOf);
  PRIMITIVE(vm->stringClass, "iterate(_)", string_iterate);
  PRIMITIVE(vm->stringClass, "iterateByte_(_)", string_iterateByte);
  PRIMITIVE(vm->stringClass, "iteratorValue(_)", string_iteratorValue);
  PRIMITIVE(vm->stringClass, "startsWith(_)", string_startsWith);
  PRIMITIVE(vm->stringClass, "toString", string_toString);

  vm->listClass = AS_CLASS(wrenFindVariable(vm, coreModule, "List"));
  PRIMITIVE(vm->listClass->obj.classObj, "new()", list_new);
  PRIMITIVE(vm->listClass, "[_]", list_subscript);
  PRIMITIVE(vm->listClass, "[_]=(_)", list_subscriptSetter);
  PRIMITIVE(vm->listClass, "add(_)", list_add);
  PRIMITIVE(vm->listClass, "addCore_(_)", list_addCore);
  PRIMITIVE(vm->listClass, "clear()", list_clear);
  PRIMITIVE(vm->listClass, "count", list_count);
  PRIMITIVE(vm->listClass, "insert(_,_)", list_insert);
  PRIMITIVE(vm->listClass, "iterate(_)", list_iterate);
  PRIMITIVE(vm->listClass, "iteratorValue(_)", list_iteratorValue);
  PRIMITIVE(vm->listClass, "removeAt(_)", list_removeAt);

  vm->mapClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Map"));
  PRIMITIVE(vm->mapClass->obj.classObj, "new()", map_new);
  PRIMITIVE(vm->mapClass, "[_]", map_subscript);
  PRIMITIVE(vm->mapClass, "[_]=(_)", map_subscriptSetter);
  PRIMITIVE(vm->mapClass, "addCore_(_,_)", map_addCore);
  PRIMITIVE(vm->mapClass, "clear()", map_clear);
  PRIMITIVE(vm->mapClass, "containsKey(_)", map_containsKey);
  PRIMITIVE(vm->mapClass, "count", map_count);
  PRIMITIVE(vm->mapClass, "remove(_)", map_remove);
  PRIMITIVE(vm->mapClass, "iterate_(_)", map_iterate);
  PRIMITIVE(vm->mapClass, "keyIteratorValue_(_)", map_keyIteratorValue);
  PRIMITIVE(vm->mapClass, "valueIteratorValue_(_)", map_valueIteratorValue);

  vm->rangeClass = AS_CLASS(wrenFindVariable(vm, coreModule, "Range"));
  PRIMITIVE(vm->rangeClass, "from", range_from);
  PRIMITIVE(vm->rangeClass, "to", range_to);
  PRIMITIVE(vm->rangeClass, "min", range_min);
  PRIMITIVE(vm->rangeClass, "max", range_max);
  PRIMITIVE(vm->rangeClass, "isInclusive", range_isInclusive);
  PRIMITIVE(vm->rangeClass, "iterate(_)", range_iterate);
  PRIMITIVE(vm->rangeClass, "iteratorValue(_)", range_iteratorValue);
  PRIMITIVE(vm->rangeClass, "toString", range_toString);

  ObjClass* systemClass = AS_CLASS(wrenFindVariable(vm, coreModule, "System"));
  PRIMITIVE(systemClass->obj.classObj, "clock", system_clock);
  PRIMITIVE(systemClass->obj.classObj, "gc()", system_gc);
  PRIMITIVE(systemClass->obj.classObj, "writeString_(_)", system_writeString);

  // While bootstrapping the core types and running the core module, a number
  // of string objects have been created, many of which were instantiated
  // before stringClass was stored in the VM. Some of them *must* be created
  // first -- the ObjClass for string itself has a reference to the ObjString
  // for its name.
  //
  // These all currently have a NULL classObj pointer, so go back and assign
  // them now that the string class is known.
  for (Obj* obj = vm->first; obj != NULL; obj = obj->next)
  {
    if (obj->type == OBJ_STRING) obj->classObj = vm->stringClass;
  }
}
