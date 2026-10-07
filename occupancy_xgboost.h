#pragma once

void xgboost_predict(double* features, double* probabilities) {
    double score = 0.0;
    
    if (features[0] < 2000.0) {
        if (features[1] > 25.0) {
            score = 0.85; 
        } else {
            score = 0.40; 
        }
    } else {
        score = 0.05;
    }
    
    probabilities[0] = 1.0 - score; 
    probabilities[1] = score;       
}