/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2019. All rights reserved.
 * Description: rename functions using macro, to solve rename conflict between pdr.a and tcpdr.a
 * Author: zhoujinning z00576985
 * Create: 2019-04-02
 */

#ifndef HIGEO50_PDR_RENAME_FUNCTION_H
#define HIGEO50_PDR_RENAME_FUNCTION_H

/* rename function to avoid function redefine confilict between pdr and tcpdr */
#ifdef WEAR_DEFINE
/* attitude_support.h */
#define Skewsm3 HigeoSkewsm3
#define GetQuatCrossMatrix HigeoGetQuatCrossMatrix
#define GetMatrixQuatMult HigeoGetMatrixQuatMult
#define QuatRotateInvJacobian HigeoQuatRotateInvJacobian
#define RotMatFromGravity HigeoRotMatFromGravity
#define GetCosSinYawByC3Magn HigeoGetCosSinYawByC3Magn
/* fusion_pdr.h */
#define PdrFusionInit HigeoPdrFusionInit
/* geo_calculate.h */
#define GetRelativeXYFromGpsPoint HigeoGetRelativeXYFromGpsPoint
#define GetPolarFromTwoPoints HigeoGetPolarFromTwoPoints
#define ConvertXYToAbsoluteLL HigeoConvertXYToAbsoluteLL
#define ConvertXYToAbsoluteLLl HigeoConvertXYToAbsoluteLLl
#define GeoEarthRadius HigeoGeoEarthRadius
#define GeoLla2Ecef HigeoGeoLla2Ecef
#define CalJacobianEcefLla HigeoCalJacobianEcefLla
#define CalJacobianLlaEnu HigeoCalJacobianLlaEnu
/* math_of_matrix.h */
#define MaxTwoComponent HigeoMaxTwoComponent
#define MaxTwoComponentD HigeoMaxTwoComponentD
#define MinTwoComponent HigeoMinTwoComponent
#define MinTwoComponentD HigeoMinTwoComponentD
#define AbsD HigeoAbsD
#define ConsecutiveAngle HigeoConsecutiveAngle
#define ConsecutiveAnglel HigeoConsecutiveAnglel
#define ConsecutiveHeadingl HigeoConsecutiveHeadingl
#define AngleDifference HigeoAngleDifference
#define AngleDifferencel HigeoAngleDifferencel
#define EyeMatrix HigeoEyeMatrix
#define EyeMatrixl HigeoEyeMatrixl
#define GetVectorNorm HigeoGetVectorNorm
#define GetVectorNorml HigeoGetVectorNorml
#define NormalizeVector HigeoNormalizeVector
#define VectorAdd HigeoVectorAdd
#define VectorMinus HigeoVectorMinus
#define VectorNegate HigeoVectorNegate
#define Dotprodl HigeoDotprodl
#define Dotprod HigeoDotprod
#define Vprod HigeoVprod
#define VectorRotationByMatrix HigeoVectorRotationByMatrix
#define VectorRotationByMatrixTrans HigeoVectorRotationByMatrixTrans
#define CMatrixProductAB HigeoCMatrixProductAB
#define CMatrixProductABl HigeoCMatrixProductABl
#define CMatrixProductTrans HigeoCMatrixProductTrans
#define CMatrixProductTransl HigeoCMatrixProductTransl
#define CMatrixInverse HigeoCMatrixInverse
#define CMatrixInversel HigeoCMatrixInversel
#define GetScalarProduct HigeoGetScalarProduct
/* pdr_api.h */
#define PdrAlgInit HigeoPdrAlgInit
#define PdrAlgDataProcess HigeoPdrAlgDataProcess
#define GetPdrResults HigeoGetPdrResults
/* tcpdr_interface.h */
#define GetPedometerSteps HigeoGetPedometerSteps
/* pdr_pedometer.h */
#define InitPedometer HigeoInitPedometer
#define PedometerSwitchingMode HigeoPedometerSwitchingMode
#define GetPedometerSteps HigeoGetPedometerSteps
#define GetStepLength HigeoGetStepLength
#define UpdateArray HigeoUpdateArray
#define g_stepCount g_stepCountWear
/* pedometer_subcode.h */
#define InitPedometerSubcode HigeoInitPedometerSubcode
#define FirFilter HigeoFirFilter
#define PedometerChooseMode HigeoPedometerChooseMode
/* sal_fusion.h */
#define FreeRotationSequence HigeoFreeRotationSequence
#define FreezeRotationSequence HigeoFreezeRotationSequence
/* pdr_fusion_interface.h */
#define g_retPdr g_retPdrHigeo
#define ResetStrapdownEstimation HigeoResetStrapdownEstimation
#define StrapdownAngleEstimation HigeoStrapdownAngleEstimation
#define KalmanMeasurementUpdatel HigeoKalmanMeasurementUpdatel
#endif

#endif // HIGEO50_PDR_RENAME_FUNCTION_H
