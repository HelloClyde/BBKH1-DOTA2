"""Release tags must identify this source version; does not create a tag."""
import os
from common import ROOT,VERSION,require
require((ROOT/'VERSION').read_text(encoding='utf8').strip()==VERSION,'Source VERSION differs')
require(os.environ.get('GITHUB_REF_TYPE')=='tag' and os.environ.get('GITHUB_REF_NAME')=='v'+VERSION,'Tag differs from source VERSION')
print('Tag/source version validated; no tag was created by this tool.')
