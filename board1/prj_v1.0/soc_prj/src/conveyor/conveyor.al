<?xml version="1.0" encoding="UTF-8"?>
<Project Version="3" Minor="2" Path="E:/26FPGA/prj_v1.0/soc_prj/src/conveyor">
    <Project_Created_Time></Project_Created_Time>
    <TD_Encoding>UTF-8</TD_Encoding>
    <TD_Version>6.2.175876</TD_Version>
    <Name>conveyor</Name>
    <HardWare>
        <Family>DR1</Family>
        <Device>DR1M90GEG400</Device>
        <Speed>-2</Speed>
    </HardWare>
    <Source_Files>
        <Verilog><File Path="conveyor_axi.v"><FileInfo>
            <Attr Name="UsedInSyn" Val="true"/>
            <Attr Name="UsedInP&R" Val="true"/>
            <Attr Name="BelongTo" Val="design_1"/>
            <Attr Name="CompileOrder" Val="1"/>
        </FileInfo></File></Verilog>
    </Source_Files>
    <FileSets>
        <FileSet Name="design_1" Type="DesignFiles">
        </FileSet>
        <FileSet Name="constraint_1" Type="ConstrainFiles">
        </FileSet>
    </FileSets>
    <TOP_MODULE>
        <LABEL>conveyor_axi</LABEL>
        <MODULE>conveyor_axi</MODULE>
        <CREATEINDEX>user</CREATEINDEX>
    </TOP_MODULE>
    <Property>
    </Property>
    <Device_Settings>
    </Device_Settings>
    <Configurations>
    </Configurations>
    <Runs>
        <Run Name="syn_1" Type="Synthesis" ConstraintSet="constraint_1" Description="" Active="true">
            <Strategy Name="Default_Synthesis_Strategy">
            </Strategy>
            <UserParams>
            </UserParams>
        </Run>
        <Run Name="phy_1" Type="PhysicalDesign" ConstraintSet="constraint_1" Description="" SynRun="syn_1" Active="true">
            <Strategy Name="Default_PhysicalDesign_Strategy">
                <BitgenProperty::GeneralOption>
                    <compress>on</compress>
                </BitgenProperty::GeneralOption>
                <PlaceProperty>
                    <opt_timing>high</opt_timing>
                </PlaceProperty>
            </Strategy>
            <UserParams>
            </UserParams>
        </Run>
    </Runs>
    <Project_Settings>
    </Project_Settings>
</Project>
