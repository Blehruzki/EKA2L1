import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmLoopFunc extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/loop_func_disasm_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();

        Address a = toAddr(0x70804534L);
        Function f = fm.getFunctionContaining(a);
        out.printf("function %s @ %s size=%d%n%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

        AddressSetView body = f.getBody();
        InstructionIterator it = listing.getInstructions(body, true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            StringBuilder regs = new StringBuilder();
            out.printf("%s  %-10s %s%n", ins.getAddress(), ins.getMnemonicString(), ins.toString());
        }

        out.close();
        println("WROTE " + outPath);
    }
}
