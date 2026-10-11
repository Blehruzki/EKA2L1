"""The kit's paths: mksis.py's template sits beside the tools."""
import os

SIS_TEMPLATE = os.environ.get('NGPORT_SIS_TEMPLATE') or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), 'EKA21LHW.sis')
