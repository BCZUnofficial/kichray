#!/usr/bin/env python3
"""Analysis must reject corrupt or miscorrelated evidence, and preserve unknowns."""
from pathlib import Path
import importlib.util
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("snapshot_analysis",ROOT/"tools/analyze_render_snapshots.py")
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)
SAMPLE="""[SNAPSHOT]
schema=3
[FRAME_IDENTITY]
session_id=9007199254740993
capture_id=7
group_id=4
engine_frame=20
render_frame=21
[COMPLETENESS]
frame_capture=COMPLETE
chronological_trace=INCOMPLETE
[CAPTURE_MODE]
mode=DETAILED_SNAPSHOT
[UI_STATE]
screen=PDA
paused=1
[TRACE_LOSS]
draw_records_observed=1001
draw_records_retained=1
draw_records_dropped=1000
[DRAW_AGGREGATE route]
draw_records_observed=1001
submitted_draw_calls=1001
semantic_type=UI
[DRAW_AGGREGATE_TOTALS]
draw_records_observed=1001
observations_lost=0
[END]
draw_events=1001
"""
class TestAnalysis(unittest.TestCase):
    def read(self,text):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"frame.log";path.write_text(text)
            return analysis.analyze(path)
    def test_overflow_is_not_failed_frame(self):
        report=self.read(SAMPLE)
        self.assertEqual(report["invariant_failures"],0)
        self.assertEqual(report["metadata"]["COMPLETENESS"]["frame_capture"],"COMPLETE")
        self.assertEqual(report["metadata"]["COMPLETENESS"]["chronological_trace"],"INCOMPLETE")
        self.assertEqual(report["metadata"]["FRAME_IDENTITY"]["session_id"],"9007199254740993")
    def test_missing_draw_is_detected(self):
        report=self.read(SAMPLE.replace("draw_records_dropped=1000","draw_records_dropped=999"))
        self.assertEqual(report["invariant_failures"],1)
    def test_truncated_file_not_silent_success(self):
        self.assertGreater(self.read(SAMPLE.split("[END]")[0])["invariant_failures"],0)
        self.assertGreater(self.read("")["invariant_failures"],0)
    def test_later_frame_gpu_result_rejected(self):
        result="\n[GPU_TIMING_COMPLETION]\ncapture_id=7\nengine_frame=20\nrender_frame=22\nstatus=READY\n"
        self.assertEqual(self.read(SAMPLE+result)["invariant_failures"],1)
        self.assertEqual(self.read(SAMPLE+result.replace("render_frame=22","render_frame=21"))["invariant_failures"],0)
    def test_sessions_not_merged(self):
        first=self.read(SAMPLE);second=self.read(SAMPLE.replace("session_id=9007199254740993","session_id=9007199254740994"))
        self.assertEqual(len(analysis.group_cycles([first,second])),2)
    def test_counter_mode_no_fake_route_requirement(self):
        text=SAMPLE.replace("mode=DETAILED_SNAPSHOT","mode=COUNTERS_ONLY")
        text=text.split("[DRAW_AGGREGATE route]")[0]+"[DRAW_AGGREGATE_TOTALS]\ndraw_records_observed=1001\nobservations_lost=0\n[END]\ndraw_events=1001\n"
        self.assertEqual(self.read(text)["invariant_failures"],0)
if __name__=="__main__":unittest.main()
