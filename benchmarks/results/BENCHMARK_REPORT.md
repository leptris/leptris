# Leptris Performance Benchmark Report

**Generated**: 2025-12-23 16:02:09

## Executive Summary

- **DOM vs libxml2**: 1.40x faster overall
- **DOM vs pugixml**: 7.91x slower overall
- **XPath vs libxml2**: 3.60x faster overall

## DOM Performance Comparison

### Combined Comparison

| Operation | Leptris | vs libxml2 | vs pugixml |
| --- | --- | --- | --- |
| Iterations: 1000 per benchmark
================================================================
  Parse + Root | 24.29 µs | 64.99 µs ✅ **2.68x faster** | 2.96 µs ⚠️ 8.21x slower |
| Tree Traversal | 940.00 ns | 460.00 ns ⚠️ 2.04x slower | 1.74 µs ✅ **1.85x faster** |
| Attribute Access (100x) | 810.00 ns | 2.69 µs ✅ **3.32x faster** | 400.00 ns ⚠️ 2.02x slower |
| Text Extraction (100x) | 20.25 µs | 8.99 µs ⚠️ 2.25x slower | 180.00 ns ⚠️ 112.50x slower |
| Child Iteration (100x) | 8.76 µs | 10.00 ns ⚠️ 876.00x slower | 1.68 µs ⚠️ 5.21x slower |
| **TOTAL** | **55.05 µs** | **77.14 µs ✅ **1.40x faster**** | **6.96 µs ⚠️ 7.91x slower** |

## XPath Performance Comparison

| Query | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| Iterations: 1000 per benchmark
================================================================
  Simple Path (//book) | 2.13 µs | 4.02 µs | ✅ **1.89x faster** |
| Predicate ([@id='101']) | 2.72 µs | 22.30 µs | ✅ **8.20x faster** |
| Function (count()) | 3.44 µs | 4.01 µs | ✅ **1.17x faster** |
| Complex Query | 5.87 µs | 35.22 µs | ✅ **6.00x faster** |
| Union (//book | //magazine) | 5.12 µs | 3.87 µs | ⚠️ 1.32x slower |
| **TOTAL** | **19.28 µs** | **69.42 µs** | ✅ **3.60x faster** |

## XPath Axes Performance

### @*[string-length() axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| @*[string-length() > 0] | 950.00 ns | 670.00 ns | ⚠️ 1.42x slower |
| **@*[string-length() Total** | **950.00 ns** | **670.00 ns** | ⚠️ 1.42x slower |

### ancestor axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| ancestor::* | 670.00 ns | 480.00 ns | ⚠️ 1.40x slower |
| ancestor::catalog | 730.00 ns | 580.00 ns | ⚠️ 1.26x slower |
| ancestor::*[@version] | 980.00 ns | 570.00 ns | ⚠️ 1.72x slower |
| **ancestor Total** | **2.38 µs** | **1.63 µs** | ⚠️ 1.46x slower |

### ancestor-or-self axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| ancestor-or-self::* | 660.00 ns | 570.00 ns | ⚠️ 1.16x slower |
| ancestor-or-self::book | 800.00 ns | 600.00 ns | ⚠️ 1.33x slower |
| ancestor-or-self::*[@id] | 1.07 µs | 650.00 ns | ⚠️ 1.65x slower |
| **ancestor-or-self Total** | **2.53 µs** | **1.82 µs** | ⚠️ 1.39x slower |

### attribute axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| attribute::* | 690.00 ns | 460.00 ns | ⚠️ 1.50x slower |
| attribute::id | 760.00 ns | 520.00 ns | ⚠️ 1.46x slower |
| **attribute Total** | **1.45 µs** | **980.00 ns** | ⚠️ 1.48x slower |

### child axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| child::* | 720.00 ns | 450.00 ns | ⚠️ 1.60x slower |
| child::book | 810.00 ns | 510.00 ns | ⚠️ 1.59x slower |
| child::*[@id] | 1.02 µs | 540.00 ns | ⚠️ 1.89x slower |
| **child Total** | **2.55 µs** | **1.50 µs** | ⚠️ 1.70x slower |

### descendant axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| descendant::* | 720.00 ns | 470.00 ns | ⚠️ 1.53x slower |
| descendant::title | 820.00 ns | 580.00 ns | ⚠️ 1.41x slower |
| descendant::*[@id] | 1.06 µs | 560.00 ns | ⚠️ 1.89x slower |
| **descendant Total** | **2.60 µs** | **1.61 µs** | ⚠️ 1.61x slower |

### descendant-or-self axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| descendant-or-self::* | 660.00 ns | 540.00 ns | ⚠️ 1.22x slower |
| descendant-or-self::book | 750.00 ns | 610.00 ns | ⚠️ 1.23x slower |
| descendant-or-self::*[@id] | 1.00 µs | 640.00 ns | ⚠️ 1.56x slower |
| **descendant-or-self Total** | **2.41 µs** | **1.79 µs** | ⚠️ 1.35x slower |

### following axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| following::* | 720.00 ns | 460.00 ns | ⚠️ 1.57x slower |
| following::title | 760.00 ns | 560.00 ns | ⚠️ 1.36x slower |
| following::*[@id] | 1.02 µs | 570.00 ns | ⚠️ 1.79x slower |
| **following Total** | **2.50 µs** | **1.59 µs** | ⚠️ 1.57x slower |

### following-sibling axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| following-sibling::* | 670.00 ns | 540.00 ns | ⚠️ 1.24x slower |
| following-sibling::book | 790.00 ns | 590.00 ns | ⚠️ 1.34x slower |
| following-sibling::*[@id] | 1.04 µs | 610.00 ns | ⚠️ 1.70x slower |
| **following-sibling Total** | **2.50 µs** | **1.74 µs** | ⚠️ 1.44x slower |

### namespace axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| namespace::* | 690.00 ns | 560.00 ns | ⚠️ 1.23x slower |
| namespace::xml | 760.00 ns | 540.00 ns | ⚠️ 1.41x slower |
| namespace::*[local-name() != 'xml'] | 1.02 µs | 820.00 ns | ⚠️ 1.24x slower |
| **namespace Total** | **2.47 µs** | **1.92 µs** | ⚠️ 1.29x slower |

### parent axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| parent::* | 680.00 ns | 470.00 ns | ⚠️ 1.45x slower |
| parent::catalog | 770.00 ns | 560.00 ns | ⚠️ 1.38x slower |
| parent::*[@version] | 1.01 µs | 590.00 ns | ⚠️ 1.71x slower |
| **parent Total** | **2.46 µs** | **1.62 µs** | ⚠️ 1.52x slower |

### preceding axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| preceding::* | 680.00 ns | 470.00 ns | ⚠️ 1.45x slower |
| preceding::title | 750.00 ns | 570.00 ns | ⚠️ 1.32x slower |
| preceding::*[@id] | 1.04 µs | 570.00 ns | ⚠️ 1.82x slower |
| **preceding Total** | **2.47 µs** | **1.61 µs** | ⚠️ 1.53x slower |

### preceding-sibling axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| preceding-sibling::* | 660.00 ns | 520.00 ns | ⚠️ 1.27x slower |
| preceding-sibling::book | 770.00 ns | 590.00 ns | ⚠️ 1.31x slower |
| preceding-sibling::*[@id] | 1.04 µs | 620.00 ns | ⚠️ 1.68x slower |
| **preceding-sibling Total** | **2.47 µs** | **1.73 µs** | ⚠️ 1.43x slower |

### self axis

| Test Case | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| self::* | 670.00 ns | 450.00 ns | ⚠️ 1.49x slower |
| self::book | 750.00 ns | 500.00 ns | ⚠️ 1.50x slower |
| self::*[@id] | 1.02 µs | 540.00 ns | ⚠️ 1.89x slower |
| **self Total** | **2.44 µs** | **1.49 µs** | ⚠️ 1.64x slower |

**Overall Axes Performance**: ⚠️ 1.48x slower

## XPath Functions Performance

### String Functions

| Function | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| string() | 2.04 µs | 11.96 µs | ✅ **5.86x faster** |
| concat() | 1.27 µs | 1.31 µs | ✅ **1.03x faster** |
| starts-with() | 940.00 ns | 1.05 µs | ✅ **1.12x faster** |
| contains() | 940.00 ns | 970.00 ns | ✅ **1.03x faster** |
| substring() | 770.00 ns | 1.06 µs | ✅ **1.38x faster** |
| substring-before() | 700.00 ns | 3.77 µs | ✅ **5.39x faster** |
| substring-after() | 690.00 ns | 5.21 µs | ✅ **7.55x faster** |
| string-length() | 590.00 ns | 860.00 ns | ✅ **1.46x faster** |
| normalize-space() | 640.00 ns | 5.46 µs | ✅ **8.53x faster** |
| translate() | 1.17 µs | 5.25 µs | ✅ **4.49x faster** |
| **String Functions Total** | **9.75 µs** | **36.90 µs** | ✅ **3.78x faster** |

### Number Functions

| Function | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| number() | 890.00 ns | 1.08 µs | ✅ **1.21x faster** |
| sum() | 1.94 µs | 7.14 µs | ✅ **3.68x faster** |
| floor() | 690.00 ns | 690.00 ns | ➖ Same |
| ceiling() | 1.24 µs | 620.00 ns | ⚠️ 2.00x slower |
| round() | 970.00 ns | 520.00 ns | ⚠️ 1.87x slower |
| **Number Functions Total** | **5.73 µs** | **10.05 µs** | ✅ **1.75x faster** |

### Boolean Functions

| Function | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| boolean() | 1.61 µs | 2.96 µs | ✅ **1.84x faster** |
| not() | 690.00 ns | 540.00 ns | ⚠️ 1.28x slower |
| true() | 580.00 ns | 380.00 ns | ⚠️ 1.53x slower |
| false() | 650.00 ns | 390.00 ns | ⚠️ 1.67x slower |
| lang() | 950.00 ns | 600.00 ns | ⚠️ 1.58x slower |
| **Boolean Functions Total** | **4.48 µs** | **4.87 µs** | ✅ **1.09x faster** |

### Nodeset Functions

| Function | Leptris | libxml2 | Comparison |
| --- | --- | --- | --- |
| last() | 1.52 µs | 8.76 µs | ✅ **5.76x faster** |
| position() | 1.62 µs | 9.05 µs | ✅ **5.59x faster** |
| count() | 2.47 µs | 3.83 µs | ✅ **1.55x faster** |
| id() | 1.73 µs | 690.00 ns | ⚠️ 2.51x slower |
| local-name() | 2.41 µs | 6.83 µs | ✅ **2.83x faster** |
| namespace-uri() | 1.45 µs | 9.22 µs | ✅ **6.36x faster** |
| name() | 1.74 µs | 83.15 µs | ✅ **47.79x faster** |
| **Nodeset Functions Total** | **12.94 µs** | **121.53 µs** | ✅ **9.39x faster** |

**Overall Functions Performance**: ✅ **5.27x faster**

---

**Legend**:
- ✅ = Leptris is faster
- ⚠️ = Leptris is slower
- ➖ = Same performance
