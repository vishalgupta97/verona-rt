---- MODULE rwlock ----
EXTENDS TLC, Integers, Sequences

minus99 == -99
tuple == [{1} -> {-1}] \cup [1..2 -> -2..1] \cup [1..3 -> -3..1] \cup [{} -> {}]

CONSTANT minValue, Tuples

ASSUME  /\ minValue \in Int
        /\ \A t \in Tuples: \A i \in 1..Len(t) : t[i] > minValue
        /\ Tuples \subseteq Seq(Int)

(*--algorithm TupleMax {
    variables inp \in Tuples,  max = minValue, i = 1 ;    
    {
    assert  (\A n \in 1..Len(inp) : inp[n] > minValue);
    while (i =< Len(inp)) {
        if (inp[i] > max) { max := inp[i] } ;
        i := i + 1
    } ;
    assert (IF inp = << >> THEN max = minValue
            ELSE /\\E n \in 1..Len(inp) : max = inp[n]
                 /\ \A n \in 1..Len(inp) : max >= inp[n])
    }
}*)

\* BEGIN TRANSLATION (chksum(pcal) = "188e176c" /\ chksum(tla) = "982344d4")
VARIABLES inp, max, i, pc

vars == << inp, max, i, pc >>

Init == (* Global variables *)
        /\ inp \in Tuples
        /\ max = minValue
        /\ i = 1
        /\ pc = "Lbl_1"

Lbl_1 == /\ pc = "Lbl_1"
         /\ Assert((\A n \in 1..Len(inp) : inp[n] > minValue), 
                   "Failure of assertion at line 16, column 5.")
         /\ pc' = "Lbl_2"
         /\ UNCHANGED << inp, max, i >>

Lbl_2 == /\ pc = "Lbl_2"
         /\ IF i =< Len(inp)
               THEN /\ IF inp[i] > max
                          THEN /\ max' = inp[i]
                          ELSE /\ TRUE
                               /\ max' = max
                    /\ i' = i + 1
                    /\ pc' = "Lbl_2"
               ELSE /\ Assert((IF inp = << >> THEN max = minValue
                               ELSE /\\E n \in 1..Len(inp) : max = inp[n]
                                    /\ \A n \in 1..Len(inp) : max >= inp[n]), 
                              "Failure of assertion at line 21, column 5.")
                    /\ pc' = "Done"
                    /\ UNCHANGED << max, i >>
         /\ inp' = inp

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == pc = "Done" /\ UNCHANGED vars

Next == Lbl_1 \/ Lbl_2
           \/ Terminating

Spec == Init /\ [][Next]_vars

Termination == <>(pc = "Done")

\* END TRANSLATION 
====
