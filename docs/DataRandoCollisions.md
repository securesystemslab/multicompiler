# Data diversity collision probability

It is in some cases possible to consistently overwrite a value in multiple variants with differing keys. This analysis shows that the probability of consistently incrementing a value in two different variants, even assuming ideal security, is 1/3. We observed this behavior in practice in the heapoffsetpov1 CFAR ATD when testing cross checking. The cross checks would pass for some pairs of variants and not others, depending on how the keys compare across variants.

Let `V` be the target value the attacker wishes to increment, `V_m` be the mask for the value `V`, and `&V` be the memory address where `V` is stored. Assume the attacker can increment a chosen memory location using mask `A_m`, that is, the program decrypts an arbitrary memory word with mask `A_m`, increments that value, and re-encrypts the value with `A_m` storing it back to memory.

Thus, our attack looks as follows:
```
    A = load V from attacker provided &V
    A = A ^ A_m    ; decrypt
    A = A + 1      ; increment A
    A = A ^ A_m    ; encrypt
    store A to &V
```
In mathematical terms, after the attack we have `V' = (V ^ V_m ^ A_m + 1) ^ A_m ^ V_m`
If this `V'` is the same in both variants, the attacker wins! The question is, how common is this?

For simplicity, assume without loss of generality that `V=0`. Let `X = V_m ^ A_m`. Thus we have that the attacker wins if `(X + 1) ^ X` is the same across variants. This happens in the following cases:

Case 1: Bit 0 of `X` is 0 in both variants. In this case, the increment will result in `(X + 1) ^ X == 1` for both variants.

Case 2: Bit 0 of `X` is 1 in both variants AND bit 1 is 0 in both variants. The increment result will be 3 in both variants.

Case N: Bits 0 through N-2 are all 1 AND bit N-1 is 0 in both variants. The increment result will be 2^N-1 in both variants.

The probability of any of the preceding cases being true is the sum of 0.25^k for all k from 1 to the value size (e.g., 32). For non-trivial value sizes, this probability comes out to 1/3.

If `V` is not 0, then the above reasoning holds, replacing `X` with `V ^ X`. The only thing that changes is the final result, since it is XORed with only `X` and not `V ^ X`, but it will still be consistent across variants.
