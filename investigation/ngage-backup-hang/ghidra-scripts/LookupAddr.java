import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.FileWriter;
import java.io.PrintWriter;

public class LookupAddr extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/lookup_addr_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath, true));

        long target = 0x80399F48L;
        Address a = toAddr(target);

        AddressSetView imageRange = currentProgram.getMemory();
        out.printf("Program: %s  Image base: %s  In range: %b%n",
            currentProgram.getName(), currentProgram.getImageBase(), imageRange.contains(a));

        Function f = getFunctionContaining(a);
        if (f != null) {
            out.printf("  Function containing 0x%x: %s @ %s (entry %s)%n", target, f.getName(), f.getBody().getMinAddress(), f.getEntryPoint());
        } else {
            out.printf("  No function containing 0x%x%n", target);
        }

        SymbolTable st = currentProgram.getSymbolTable();
        Symbol[] syms = st.getSymbols(a);
        for (Symbol s : syms) {
            out.printf("  Symbol at 0x%x: %s (%s)%n", target, s.getName(), s.getSymbolType());
        }

        out.close();
        println("WROTE " + outPath);
    }
}
