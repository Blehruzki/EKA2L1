import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 37 continued: FUN_83485e24 and its wrapper FUN_83485fde both call straight
// through with no return-value check at all. Need to find whoever calls
// FUN_83485fde (0x83485fde) to see if THAT caller loops on it and/or checks a
// result - disassemble the whole binary and scan for "bl 0x83485fde".
public class FindCdlserverCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address start = toAddr(0x83485248L);
        Address end = toAddr(0x83486c78L);

        clearListing(start, end);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, start, end, BigInteger.ONE);
        }

        // Seed disassembly at every 2-byte-aligned address in range - crude but
        // effective for a small (6.5KB) binary with no auto-analysis available;
        // Thumb code disassembled from a wrong offset just produces garbage that
        // won't match our "bl 0x83485fde" text search below, which only fires on
        // clean, unambiguous decodes anyway.
        for (long a = 0x83485248L; a < 0x83486c78L; a += 2) {
            try {
                Address addr = toAddr(a);
                if (listing.getInstructionAt(addr) == null) {
                    disassemble(addr);
                }
            } catch (Exception e) {
                // keep going
            }
        }

        out.println("=== instructions referencing 0x83485fde ===");
        InstructionIterator ins = listing.getInstructions(start, true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            if (i.getAddress().compareTo(end) > 0) break;
            String text = i.toString();
            if (text.contains("83485fde")) {
                out.printf("%s: %s%n", i.getAddress(), text);
            }
        }

        out.println();
        out.println("=== instructions referencing 0x83485e24 ===");
        ins = listing.getInstructions(start, true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            if (i.getAddress().compareTo(end) > 0) break;
            String text = i.toString();
            if (text.contains("83485e24")) {
                out.printf("%s: %s%n", i.getAddress(), text);
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
