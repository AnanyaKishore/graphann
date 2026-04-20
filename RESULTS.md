# ANN Result Comparison: Baseline vs Two-Pass Vamana

## Takeaways

- Two-Pass Vamana is the better overall choice for practical ANN search: much lower latency and tail latency with only a small recall drop at most L values.
- Recall@10 is very close between methods. Baseline is slightly higher for L >= 20, but gaps are small (largest gap about 1.0 percentage point at L=20).
- Two-Pass Vamana uses consistently fewer distance computations at every L (about 15% to 26% fewer; ~17.8% fewer on average).
- Two-Pass Vamana has much lower average latency at every L (about 54% to 66% lower; ~60.6% lower on average).
- Two-Pass Vamana dramatically improves P99 latency at every L (~72.5% lower on average), which is a major win for stability and production tail behavior.

## Interpretation

- If your priority is speed, throughput, and consistent latency, Two-Pass Vamana clearly wins.
- If your priority is absolute highest recall at the same L, baseline is marginally better in most rows.
- Overall Pareto view: Two-Pass Vamana gives near-equivalent recall for a very large latency reduction, which is usually the preferred tradeoff.

## Raw Results

Baseline Algorithm
```
=== Search Results (K=10) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.7751           642.6             488.2            3344.2
      20        0.8913           884.0             885.9            4892.0
      30        0.9331          1101.2             934.6            2117.6
      50        0.9665          1510.5            1412.9            3536.7
      75        0.9820          1987.6            2259.2            8222.0
     100        0.9892          2437.2            2437.0            7830.1
     150        0.9939          3272.1            3491.1            8633.0
     200        0.9961          4046.8            5099.8           13135.3
```
Two-Pass Vamana
```
=== Search Results (K=10) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.7779           476.8             184.3             385.8
      20        0.8813           675.0             301.3             984.6
      30        0.9235           860.2             427.8            1862.8
      50        0.9595          1212.6             567.1            1487.1
      75        0.9770          1627.6             762.4            1187.9
     100        0.9851          2018.8            1020.6            2503.1
     150        0.9917          2751.5            1472.0            2609.2
     200        0.9949          3433.7            1962.9            3202.9
```
