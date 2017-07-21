# sendfaxvsi
Convert output codes in VSI-FAX documents to HylaFax+command line options

VSI-Fax allows embedding output controls like phone number and name in the output document. sendfaxvsi scans the document for these codes, filters them out, and turns them into switches for HylaFax+ sendfax. VSI-Fax allows multiple different output controls per document. This is not fully implemented.

Original line:
````
printf "@+VFX[tfn=5175550116;tnm=Operator]\rTest fax\n" | /usr/vsifax3/bin/vfx -d "class1" -m both -F txt -C modern2
````
New line:
````
printf "@+VFX[tfn=5175550116;tnm=Operator]\rTest fax\n" | sendfaxvsi sendfax -m
````
