---- MODULE rwlock ----
EXTENDS TLC, Integers, Sequences, FiniteSets

Procs == {0,1,2}

(*--fair algorithm Alternate {
    variable turn \in Procs;
    fair process (p \in Procs) {
        ncs: while(TRUE) {
                skip;
        enter:  await turn = self;
        cs:     skip;
        exit:   turn := Cardinality(Procs) - 1 - self;
        };
        goto ncs;
    }
}*)

\* BEGIN TRANSLATION (chksum(pcal) = "e1058418" /\ chksum(tla) = "ba633a3d")
VARIABLES turn, pc

vars == << turn, pc >>

ProcSet == (Procs)

Init == (* Global variables *)
        /\ turn \in Procs
        /\ pc = [self \in ProcSet |-> "ncs"]

ncs(self) == /\ pc[self] = "ncs"
             /\ TRUE
             /\ pc' = [pc EXCEPT ![self] = "enter"]
             /\ turn' = turn

enter(self) == /\ pc[self] = "enter"
               /\ turn = self
               /\ pc' = [pc EXCEPT ![self] = "cs"]
               /\ turn' = turn

cs(self) == /\ pc[self] = "cs"
            /\ TRUE
            /\ pc' = [pc EXCEPT ![self] = "exit"]
            /\ turn' = turn

exit(self) == /\ pc[self] = "exit"
              /\ turn' = Cardinality(Procs) - 1 - self
              /\ pc' = [pc EXCEPT ![self] = "ncs"]

p(self) == ncs(self) \/ enter(self) \/ cs(self) \/ exit(self)

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == (\E self \in Procs: p(self))
           \/ Terminating

Spec == /\ Init /\ [][Next]_vars
        /\ WF_vars(Next)
        /\ \A self \in Procs : WF_vars(p(self))

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION 

MutualExclusion == \A i, j \in Procs: (i /= j) => ~((pc[i] = "cs") /\ pc[j] = "cs")

Liveness == \A i \in Procs: []<>(pc[i] = "cs")
====
