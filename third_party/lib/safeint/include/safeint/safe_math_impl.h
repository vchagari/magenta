// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_NUMERICS_SAFE_MATH_IMPL_H_
#define BASE_NUMERICS_SAFE_MATH_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <stdlib.h>
#include <utils/limits.h>
#include <utils/type_support.h>

#include <safeint/safe_conversions.h>

namespace safeint {
namespace internal {

template <typename T> T abs(T n) {
    if (n >= 0) {
        return n;
    } else {
        return static_cast<T>(-n);
    }
}

// Everything from here up to the floating point operations is portable C++,
// but it may not be fast. This code could be split based on
// platform/architecture and replaced with potentially faster implementations.

// Integer promotion templates used by the portable checked integer arithmetic.
template <size_t Size, bool IsSigned>
struct IntegerForSizeAndSign;
template <>
struct IntegerForSizeAndSign<1, true> {
  typedef int8_t type;
};
template <>
struct IntegerForSizeAndSign<1, false> {
  typedef uint8_t type;
};
template <>
struct IntegerForSizeAndSign<2, true> {
  typedef int16_t type;
};
template <>
struct IntegerForSizeAndSign<2, false> {
  typedef uint16_t type;
};
template <>
struct IntegerForSizeAndSign<4, true> {
  typedef int32_t type;
};
template <>
struct IntegerForSizeAndSign<4, false> {
  typedef uint32_t type;
};
template <>
struct IntegerForSizeAndSign<8, true> {
  typedef int64_t type;
};
template <>
struct IntegerForSizeAndSign<8, false> {
  typedef uint64_t type;
};

// WARNING: We have no IntegerForSizeAndSign<16, *>. If we ever add one to
// support 128-bit math, then the ArithmeticPromotion template below will need
// to be updated (or more likely replaced with a decltype expression).

template <typename Integer>
struct UnsignedIntegerForSize {
  typedef typename utils::enable_if<
      utils::numeric_limits<Integer>::is_integer,
      typename IntegerForSizeAndSign<sizeof(Integer), false>::type>::type type;
};

template <typename Integer>
struct SignedIntegerForSize {
  typedef typename utils::enable_if<
      utils::numeric_limits<Integer>::is_integer,
      typename IntegerForSizeAndSign<sizeof(Integer), true>::type>::type type;
};

template <typename Integer>
struct TwiceWiderInteger {
  typedef typename utils::enable_if<
      utils::numeric_limits<Integer>::is_integer,
      typename IntegerForSizeAndSign<
          sizeof(Integer) * 2,
          utils::numeric_limits<Integer>::is_signed>::type>::type type;
};

template <typename Integer>
struct PositionOfSignBit {
  static const typename utils::enable_if<utils::numeric_limits<Integer>::is_integer,
                                       size_t>::type value =
      8 * sizeof(Integer) - 1;
};

// This is used for UnsignedAbs, where we need to support floating-point
// template instantiations even though we don't actually support the operations.
// However, there is no corresponding implementation of e.g. CheckedUnsignedAbs,
// so the float versions will not compile.
template <typename Numeric,
          bool IsInteger = utils::numeric_limits<Numeric>::is_integer>
struct UnsignedOrFloatForSize;

template <typename Numeric>
struct UnsignedOrFloatForSize<Numeric, true> {
  typedef typename UnsignedIntegerForSize<Numeric>::type type;
};

// Helper templates for integer manipulations.

template <typename T>
bool HasSignBit(T x) {
  // Cast to unsigned since right shift on signed is undefined.
  return !!(static_cast<typename UnsignedIntegerForSize<T>::type>(x) >>
            PositionOfSignBit<T>::value);
}

// This wrapper undoes the standard integer promotions.
template <typename T>
T BinaryComplement(T x) {
  return ~x;
}

// Here are the actual portable checked integer math implementations.
// TODO(jschuh): Break this code out from the enable_if pattern and find a clean
// way to coalesce things into the CheckedNumericState specializations below.

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer, T>::type
CheckedAdd(T x, T y, RangeConstraint* validity) {
  // Since the value of x+y is undefined if we have a signed type, we compute
  // it using the unsigned type of the same size.
  typedef typename UnsignedIntegerForSize<T>::type UnsignedDst;
  UnsignedDst ux = static_cast<UnsignedDst>(x);
  UnsignedDst uy = static_cast<UnsignedDst>(y);
  UnsignedDst uresult = ux + uy;
  // Addition is valid if the sign of (x + y) is equal to either that of x or
  // that of y.
  if (utils::numeric_limits<T>::is_signed) {
    if (HasSignBit(BinaryComplement((uresult ^ ux) & (uresult ^ uy))))
      *validity = RANGE_VALID;
    else  // Direction of wrap is inverse of result sign.
      *validity = HasSignBit(uresult) ? RANGE_OVERFLOW : RANGE_UNDERFLOW;

  } else {  // Unsigned is either valid or overflow.
    *validity = BinaryComplement(x) >= y ? RANGE_VALID : RANGE_OVERFLOW;
  }
  return static_cast<T>(uresult);
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer, T>::type
CheckedSub(T x, T y, RangeConstraint* validity) {
  // Since the value of x+y is undefined if we have a signed type, we compute
  // it using the unsigned type of the same size.
  typedef typename UnsignedIntegerForSize<T>::type UnsignedDst;
  UnsignedDst ux = static_cast<UnsignedDst>(x);
  UnsignedDst uy = static_cast<UnsignedDst>(y);
  UnsignedDst uresult = ux - uy;
  // Subtraction is valid if either x and y have same sign, or (x-y) and x have
  // the same sign.
  if (utils::numeric_limits<T>::is_signed) {
    if (HasSignBit(BinaryComplement((uresult ^ ux) & (ux ^ uy))))
      *validity = RANGE_VALID;
    else  // Direction of wrap is inverse of result sign.
      *validity = HasSignBit(uresult) ? RANGE_OVERFLOW : RANGE_UNDERFLOW;

  } else {  // Unsigned is either valid or underflow.
    *validity = x >= y ? RANGE_VALID : RANGE_UNDERFLOW;
  }
  return static_cast<T>(uresult);
}

// Integer multiplication is a bit complicated. In the fast case we just
// we just promote to a twice wider type, and range check the result. In the
// slow case we need to manually check that the result won't be truncated by
// checking with division against the appropriate bound.
template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            sizeof(T) * 2 <= sizeof(uintmax_t),
                        T>::type
CheckedMul(T x, T y, RangeConstraint* validity) {
  typedef typename TwiceWiderInteger<T>::type IntermediateType;
  IntermediateType tmp =
      static_cast<IntermediateType>(x) * static_cast<IntermediateType>(y);
  *validity = DstRangeRelationToSrcRange<T>(tmp);
  return static_cast<T>(tmp);
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            utils::numeric_limits<T>::is_signed &&
                            (sizeof(T) * 2 > sizeof(uintmax_t)),
                        T>::type
CheckedMul(T x, T y, RangeConstraint* validity) {
  // If either side is zero then the result will be zero.
  if (!x || !y) {
    return RANGE_VALID;

  } else if (x > 0) {
    if (y > 0)
      *validity =
          x <= utils::numeric_limits<T>::max() / y ? RANGE_VALID : RANGE_OVERFLOW;
    else
      *validity = y >= utils::numeric_limits<T>::min() / x ? RANGE_VALID
                                                         : RANGE_UNDERFLOW;

  } else {
    if (y > 0)
      *validity = x >= utils::numeric_limits<T>::min() / y ? RANGE_VALID
                                                         : RANGE_UNDERFLOW;
    else
      *validity =
          y >= utils::numeric_limits<T>::max() / x ? RANGE_VALID : RANGE_OVERFLOW;
  }

  return x * y;
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            !utils::numeric_limits<T>::is_signed &&
                            (sizeof(T) * 2 > sizeof(uintmax_t)),
                        T>::type
CheckedMul(T x, T y, RangeConstraint* validity) {
  *validity = (y == 0 || x <= utils::numeric_limits<T>::max() / y)
                  ? RANGE_VALID
                  : RANGE_OVERFLOW;
  return x * y;
}

// Division just requires a check for an invalid negation on signed min/-1.
template <typename T>
T CheckedDiv(T x,
             T y,
             RangeConstraint* validity,
             typename utils::enable_if<utils::numeric_limits<T>::is_integer,
                                     int>::type = 0) {
  if (utils::numeric_limits<T>::is_signed && x == utils::numeric_limits<T>::min() &&
      y == static_cast<T>(-1)) {
    *validity = RANGE_OVERFLOW;
    return utils::numeric_limits<T>::min();
  }

  *validity = RANGE_VALID;
  return x / y;
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedMod(T x, T y, RangeConstraint* validity) {
  *validity = y > 0 ? RANGE_VALID : RANGE_INVALID;
  return x % y;
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            !utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedMod(T x, T y, RangeConstraint* validity) {
  *validity = RANGE_VALID;
  return x % y;
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedNeg(T value, RangeConstraint* validity) {
  *validity =
      value != utils::numeric_limits<T>::min() ? RANGE_VALID : RANGE_OVERFLOW;
  // The negation of signed min is min, so catch that one.
  return static_cast<T>(-value);
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            !utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedNeg(T value, RangeConstraint* validity) {
  // The only legal unsigned negation is zero.
  *validity = value ? RANGE_UNDERFLOW : RANGE_VALID;
  return static_cast<T>(
      -static_cast<typename SignedIntegerForSize<T>::type>(value));
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedAbs(T value, RangeConstraint* validity) {
  *validity =
      value != utils::numeric_limits<T>::min() ? RANGE_VALID : RANGE_OVERFLOW;
  return static_cast<T>(abs(value));
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            !utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedAbs(T value, RangeConstraint* validity) {
  // T is unsigned, so |value| must already be positive.
  *validity = RANGE_VALID;
  return value;
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            utils::numeric_limits<T>::is_signed,
                        typename UnsignedIntegerForSize<T>::type>::type
CheckedUnsignedAbs(T value) {
  typedef typename UnsignedIntegerForSize<T>::type UnsignedT;
  return value == utils::numeric_limits<T>::min()
             ? static_cast<UnsignedT>(static_cast<UnsignedT>(utils::numeric_limits<T>::max()) + 1)
             : static_cast<UnsignedT>(abs(value));
}

template <typename T>
typename utils::enable_if<utils::numeric_limits<T>::is_integer &&
                            !utils::numeric_limits<T>::is_signed,
                        T>::type
CheckedUnsignedAbs(T value) {
  // T is unsigned, so |value| must already be positive.
  return value;
}

// Floats carry around their validity state with them, but integers do not. So,
// we wrap the underlying value in a specialization in order to hide that detail
// and expose an interface via accessors.
enum NumericRepresentation {
  NUMERIC_INTEGER,
  NUMERIC_UNKNOWN
};

template <typename NumericType>
struct GetNumericRepresentation {
  static const NumericRepresentation value =
      utils::numeric_limits<NumericType>::is_integer
          ? NUMERIC_INTEGER
          : NUMERIC_UNKNOWN;
};

template <typename T, NumericRepresentation type =
                          GetNumericRepresentation<T>::value>
class CheckedNumericState {};

// Integrals require quite a bit of additional housekeeping to manage state.
template <typename T>
class CheckedNumericState<T, NUMERIC_INTEGER> {
 private:
  T value_;
  RangeConstraint validity_;

 public:
  template <typename Src, NumericRepresentation type>
  friend class CheckedNumericState;

  CheckedNumericState() : value_(0), validity_(RANGE_VALID) {}

  template <typename Src>
  CheckedNumericState(Src value, RangeConstraint validity)
      : value_(static_cast<T>(value)),
        validity_(GetRangeConstraint(validity |
                                     DstRangeRelationToSrcRange<T>(value))) {
    static_assert(utils::numeric_limits<Src>::is_specialized,
                  "Argument must be numeric.");
  }

  // Copy constructor.
  template <typename Src>
  CheckedNumericState(const CheckedNumericState<Src>& rhs)
      : value_(static_cast<T>(rhs.value())),
        validity_(GetRangeConstraint(
            rhs.validity() | DstRangeRelationToSrcRange<T>(rhs.value()))) {}

  template <typename Src>
  explicit CheckedNumericState(
      Src value,
      typename utils::enable_if<utils::numeric_limits<Src>::is_specialized,
                              int>::type = 0)
      : value_(static_cast<T>(value)),
        validity_(DstRangeRelationToSrcRange<T>(value)) {}

  RangeConstraint validity() const { return validity_; }
  T value() const { return value_; }
};

// For integers less than 128-bit and floats 32-bit or larger, we can distil
// C/C++ arithmetic promotions down to two simple rules:
// 1. The type with the larger maximum exponent always takes precedence.
// 2. The resulting type must be promoted to at least an int.
// The following template specializations implement that promotion logic.
enum ArithmeticPromotionCategory {
  LEFT_PROMOTION,
  RIGHT_PROMOTION,
  DEFAULT_PROMOTION
};

template <typename Lhs,
          typename Rhs = Lhs,
          ArithmeticPromotionCategory Promotion =
              (MaxExponent<Lhs>::value > MaxExponent<Rhs>::value)
                  ? (MaxExponent<Lhs>::value > MaxExponent<int>::value
                         ? LEFT_PROMOTION
                         : DEFAULT_PROMOTION)
                  : (MaxExponent<Rhs>::value > MaxExponent<int>::value
                         ? RIGHT_PROMOTION
                         : DEFAULT_PROMOTION) >
struct ArithmeticPromotion;

template <typename Lhs, typename Rhs>
struct ArithmeticPromotion<Lhs, Rhs, LEFT_PROMOTION> {
  typedef Lhs type;
};

template <typename Lhs, typename Rhs>
struct ArithmeticPromotion<Lhs, Rhs, RIGHT_PROMOTION> {
  typedef Rhs type;
};

template <typename Lhs, typename Rhs>
struct ArithmeticPromotion<Lhs, Rhs, DEFAULT_PROMOTION> {
  typedef int type;
};

// We can statically check if operations on the provided types can wrap, so we
// can skip the checked operations if they're not needed. So, for an integer we
// care if the destination type preserves the sign and is twice the width of
// the source.
template <typename T, typename Lhs, typename Rhs>
struct IsIntegerArithmeticSafe {
  static const bool value = StaticDstRangeRelationToSrcRange<T, Lhs>::value ==
                                NUMERIC_RANGE_CONTAINED &&
                            sizeof(T) >= (2 * sizeof(Lhs)) &&
                            StaticDstRangeRelationToSrcRange<T, Rhs>::value !=
                                NUMERIC_RANGE_CONTAINED &&
                            sizeof(T) >= (2 * sizeof(Rhs));
};

}  // namespace internal
}  // namespace safeint

#endif  // BASE_NUMERICS_SAFE_MATH_IMPL_H_