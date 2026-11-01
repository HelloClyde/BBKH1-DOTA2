#ifndef H1_DOTA_WARD_PLACEMENT_DATA_H
#define H1_DOTA_WARD_PLACEMENT_DATA_H
/* Installed default-active trigger_no_wards: five original PHYS models,
 * seven convex XY projections. Keep each hull and take their union; no
 * navigation clearance, healing-radius approximation, or FOW expansion.
 * Q16 preserves every integer XY membership of the original float
 * projections in the audited hull bounding boxes. Source evidence:
 * provenance/ward-placement/source-data/source-audit.json. Ground-point
 * placement does not simulate arbitrary flying altitude. */
#define DOTA_WARD_FORBIDDEN_HULLS 7
static const unsigned char dota_ward_forbidden_offsets[DOTA_WARD_FORBIDDEN_HULLS+1]={0,4,8,12,16,24,31,35};
static const unsigned char dota_ward_forbidden_entity[DOTA_WARD_FORBIDDEN_HULLS]={0,0,1,1,2,3,4};
static const int dota_ward_forbidden_q16[35][2]={
    {21546544,227540992},
    {32505856,238225616},
    {32505856,253362176},
    {21546544,253362176},
    {4194304,227540992},
    {21546544,227540992},
    {21546544,253362176},
    {4194304,253362176},
    {235929600,17825792},
    {243269632,17825792},
    {243269632,40894464},
    {235929600,33554432},
    {243269624,17825792},
    {262144008,17825792},
    {262144008,40894464},
    {243269624,40894464},
    {75598752,99113364},
    {76569242,89919243},
    {82297536,88155127},
    {85830416,88559228},
    {88975093,92640987},
    {88975095,92640992},
    {91496715,100931833},
    {84083538,105108659},
    {170112868,178883817},
    {177448071,171286952},
    {188042862,174026685},
    {191097836,180872400},
    {186776179,185947077},
    {182105410,186864019},
    {175856686,185570730},
    {211267690,153152848},
    {213882597,149553752},
    {218599371,152522462},
    {215466631,156203576},
};
static const int dota_ward_forbidden_bounds_q16[DOTA_WARD_FORBIDDEN_HULLS][4]={
    {21546544,32505856,227540992,253362176},
    {4194304,21546544,227540992,253362176},
    {235929600,243269632,17825792,40894464},
    {243269624,262144008,17825792,40894464},
    {75598752,91496715,88155127,105108659},
    {170112868,191097836,171286952,186864019},
    {211267690,218599371,149553752,156203576},
};
#endif
