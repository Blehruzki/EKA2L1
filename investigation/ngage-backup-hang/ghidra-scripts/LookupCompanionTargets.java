import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;

public class LookupCompanionTargets extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/lookup_companion_targets_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath, true));

        long[] targets = {
            0x803aa0f6L, 0x803aa364L, 0x803aa398L, 0x803aa9aaL,
            0x803a5c06L, 0x803a83aeL, 0x803a56a6L, 0x803a56baL,
            0x8039ccb2L, 0x803a0478L, 0x803a0934L, 0x803a09dcL,
            0x803a6de4L, 0x803a73b6L, 0x803a7e8aL, 0x803a82aaL,
            0x803a8da8L, 0x803a96daL, 0x803a9f34L, 0x803ab7caL,
            0x803ad218L, 0x803af4a6L, 0x803b069eL, 0x803b0806L,
            0x803b09aeL, 0x803b0b5cL, 0x803b1d1eL, 0x803b2a06L,
            0x803b6864L, 0x804c69eeL
        };

        SymbolTable st = currentProgram.getSymbolTable();
        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            String fname = (f != null) ? (f.getName() + " @ " + f.getEntryPoint()) : "(no function)";
            Symbol[] syms = st.getSymbols(a);
            StringBuilder symNames = new StringBuilder();
            for (Symbol s : syms) {
                symNames.append(s.getName()).append(" ");
            }
            out.printf("0x%x -> %s  symbols: %s%n", target, fname, symNames.toString());
        }

        out.close();
        println("WROTE " + outPath);
    }
}
