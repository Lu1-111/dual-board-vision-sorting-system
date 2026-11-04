// SPDX-License-Identifier: GPL-2.0
/*
 * Anlogic dynamic clock Driver - Simplified version (Fractional divider only, Single channel PLL)
 *
 * Copyright (c) 2025 anlogic Technologies Co., Ltd.
 * 20250913 suport simplified fractional mode
 */


#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/delay.h>

// PLL register address definitions
#define PLL_N_ADDR              0x04    // N divider coefficient register address
#define PLL_M_ADDR              0x12    // M divider coefficient register address
#define PLL_M_FRAC_LOW_ADDR     0x10    // M divider fractional part low 8 bits register address
#define PLL_M_FRAC_HIGH_ADDR    0x11    // M divider fractional part high 4 bits register address
#define PLL_C0_DIV_ADDR         0x27    // C0 divider register address
#define PLL_C0_DUTY_ADDR        0x24    // C0 duty cycle register address


// bus address offset definitions
#define PLL_BASE_ADDR           0x00    // PLL base address offset
#define PLL_OUTPUT_CTRL_ADDR    0x0C    // Output clock path control register address
#define PLL_LOCK_STATUS_ADDR    0x08    // PLL lock status register address

// PLL parameter definitions - optimized for higher VCO
#define PLL_VCO_MIN_HZ      800000000UL   // VCO minimum frequency (Hz)
#define PLL_VCO_MAX_HZ      2000000000UL  // VCO maximum frequency (Hz)
#define PLL_VCO_PREFERRED_MIN_HZ  1600000000UL  // Preferred VCO minimum for better performance
#define PLL_N_MIN           1             // N divider minimum value
#define PLL_N_MAX           127           // N divider maximum value
#define PLL_M_INT_MIN       3             // M divider integer part minimum value
#define PLL_M_INT_MAX       126           // M divider integer part maximum value
#define PLL_C_MIN           1             // C divider minimum value
#define PLL_C_MAX           128           // C divider maximum value
#define FRAC_PRECISION      4096          // 12-bit fractional precision (2^12)
#define MIN_DUTY_CYCLES     4             // Minimum duty cycles for reasonable duty ratio

// PLL register write macro
#define pll_write_reg(base, reg, val) do { \
    printk("PLL write: reg=0x%02x, value=0x%02x\n", (reg), (val)); \
    writel(((val) | ((reg<<8)&0xff00)), (base) ); \
} while(0)

struct dynclk_dev {
    struct platform_device *pdev;
    void __iomem *base;
    unsigned int refclk;  // Hz unit
    unsigned int expclk;  // Hz unit
};

// PLL configuration structure
typedef struct {
    unsigned long clk_in_hz;        // Input clock frequency (Hz)
    unsigned long clk_out_hz;       // Output clock frequency (Hz)
    
    // Calculated PLL parameters
    int n_div;                      // N divider value
    int m_div_int;                  // M divider integer part
    int m_div_frac;                 // M divider fractional part (0-4095)
    int c_div;                      // C divider value
    unsigned long vco_freq_hz;      // VCO frequency (Hz)
    unsigned long actual_clk_out_hz;// Actual output frequency (Hz)
    int error_ppm;                  // Error in parts per million
    int duty_ratio_percent;         // Duty ratio as percentage
} pll_config_t;

//******************************************************************************
// Function name : calculate_pll_params_simplified
// Function      : Calculate PLL parameters using simplified fractional approach
//                 VCO and M are scaled by 4096 to handle fractional calculations
//******************************************************************************
static int calculate_pll_params_simplified(pll_config_t *config) {
    int n, c;
    int best_n = 0, best_c = 0, best_m_scaled = 0;
    int best_error = INT_MAX;
    
    if (config == NULL) {
        return -1;
    }

    printk("Simplified PLL calculation: input=%lu Hz, target=%lu Hz\n", 
           config->clk_in_hz, config->clk_out_hz);

    // Try all possible N and C combinations
    for (n = PLL_N_MIN; n <= PLL_N_MAX; n++) {
        for (c = PLL_C_MIN; c <= PLL_C_MAX; c++) {
            // Calculate required VCO frequency (scaled by 4096)
            // VCO = clk_out * C * 4096
            unsigned long long vco_scaled = (unsigned long long)config->clk_out_hz * c * FRAC_PRECISION;
            
            // Calculate M value (scaled by 4096)
            // M_scaled = VCO_scaled * N / clk_in
            unsigned long long m_scaled = (vco_scaled * n) / config->clk_in_hz;
            
            // Check M range: M_int should be between PLL_M_INT_MIN and PLL_M_INT_MAX
            int m_int = (int)(m_scaled / FRAC_PRECISION);
            int m_frac = (int)(m_scaled % FRAC_PRECISION);
            
            if (m_int < PLL_M_INT_MIN || m_int > PLL_M_INT_MAX) {
                continue;
            }
            
            // Calculate actual VCO frequency
            // VCO = clk_in * (M_int + M_frac/4096) / N
            unsigned long long actual_vco_hz = (config->clk_in_hz * m_scaled) / ((unsigned long long)n * FRAC_PRECISION);
            
            // Check VCO frequency range
            if (actual_vco_hz < PLL_VCO_MIN_HZ || actual_vco_hz > PLL_VCO_MAX_HZ) {
                continue;
            }
            
            // Calculate actual output frequency
            unsigned long long actual_out_hz = actual_vco_hz / c;
            
            // Calculate error in PPM
            unsigned long long diff = (actual_out_hz > config->clk_out_hz) ? 
                                     (actual_out_hz - config->clk_out_hz) : 
                                     (config->clk_out_hz - actual_out_hz);
            int error_ppm = (int)((diff * 1000000ULL) / config->clk_out_hz);
            
            // Quality scoring: prefer higher VCO and reasonable C divider
            int quality_score = 0;
            if (actual_vco_hz > PLL_VCO_PREFERRED_MIN_HZ) quality_score += 100;
            if (c >= MIN_DUTY_CYCLES) quality_score += 50;
            if (c % 2 == 0) quality_score += 25; // Bonus for even C divider
            
            // Select best solution: minimize error, then maximize quality
            bool is_better = false;
            if (error_ppm < best_error) {
                is_better = true;
            } else if (error_ppm == best_error && quality_score > 0) {
                is_better = true;
            }
            
            if (is_better) {
                best_error = error_ppm;
                best_n = n;
                best_c = c;
                best_m_scaled = (int)m_scaled;
                
                // Early exit for excellent solutions
                if (error_ppm <= 10) {
                    break;
                }
            }
        }
        
        // Early exit for excellent solutions
        if (best_error <= 10) {
            break;
        }
    }

    // Check if we found a valid solution
    if (best_n == 0 || best_c == 0 || best_m_scaled == 0) {
        printk("Failed to find valid PLL parameters\n");
        return -1;
    }

    // Extract integer and fractional parts from scaled M value
    config->n_div = best_n;
    config->c_div = best_c;
    config->m_div_int = best_m_scaled / FRAC_PRECISION;  // Integer part
    config->m_div_frac = best_m_scaled % FRAC_PRECISION; // Fractional part (0-4095)
    
    // Calculate final results
    unsigned long long m_total_scaled = (unsigned long long)config->m_div_int * FRAC_PRECISION + config->m_div_frac;
    config->vco_freq_hz = (config->clk_in_hz * m_total_scaled) / ((unsigned long long)config->n_div * FRAC_PRECISION);
    config->actual_clk_out_hz = config->vco_freq_hz / config->c_div;
    config->error_ppm = best_error;
    config->duty_ratio_percent = (config->c_div % 2 == 0) ? 50 : ((config->c_div + 1) * 50) / config->c_div;

    printk("PLL parameters calculated successfully:\n");
    return 0;
}

//******************************************************************************
// Function name : dynclk_set
// Function      : Configure PLL with simplified calculation method
//******************************************************************************
static int dynclk_set(struct dynclk_dev *clkdev) {
    pll_config_t pll_config;
    void __iomem *base = clkdev->base;
    unsigned int best_duty;
    unsigned int frac_low, frac_high;
    
    memset(&pll_config, 0, sizeof(pll_config));
    
    // Configure input and output frequencies
    pll_config.clk_in_hz = clkdev->refclk;
    pll_config.clk_out_hz = clkdev->expclk;
    
    // Calculate PLL parameters with simplified method
    if (calculate_pll_params_simplified(&pll_config) != 0) {
        dev_err(&clkdev->pdev->dev, "Unable to find suitable PLL configuration\n");
        return -EINVAL;
    }
    
    dev_info(&clkdev->pdev->dev, "PLL config: VCO=%lu Hz, N=%d, M=%d.%04d, C=%d\n",
        pll_config.vco_freq_hz, pll_config.n_div, pll_config.m_div_int, 
        (pll_config.m_div_frac * 10000) / FRAC_PRECISION, pll_config.c_div);
    
    dev_info(&clkdev->pdev->dev, "Output: expected=%u Hz, actual=%lu Hz, error=%d ppm, duty=%d%%\n",
        clkdev->expclk, pll_config.actual_clk_out_hz, pll_config.error_ppm,
        pll_config.duty_ratio_percent);
    
    // Disable output before configuration
    writel(0x0, base + PLL_OUTPUT_CTRL_ADDR);
    
    // Write PLL registers in optimal order
    pll_write_reg(base, PLL_N_ADDR, (pll_config.n_div - 1) & 0xff);
    
    // Write M integer part with enable bit (bit 7)
    pll_write_reg(base, PLL_M_ADDR, ((pll_config.m_div_int - 1) | 0x80) & 0xff);
    
    // Write M fractional part
    frac_low = pll_config.m_div_frac & 0xFF;
    frac_high = (pll_config.m_div_frac >> 8) & 0x0F;
    pll_write_reg(base, PLL_M_FRAC_LOW_ADDR, frac_low);
    pll_write_reg(base, PLL_M_FRAC_HIGH_ADDR, frac_high);
    
    // Write C divider and optimized duty cycle
    pll_write_reg(base, PLL_C0_DIV_ADDR, (pll_config.c_div - 1) & 0xff);
    
    // Calculate optimal duty cycle value
    if (pll_config.c_div % 2 == 0) {
        best_duty = pll_config.c_div / 2;  // Perfect 50% for even dividers
    } else {
        best_duty = (pll_config.c_div + 1) / 2;  // Close to 50% for odd dividers
    }
    pll_write_reg(base, PLL_C0_DUTY_ADDR, (best_duty) & 0xff);
    
    // Small delay to allow PLL to stabilize
    for (int i = 0; i < 100; i++) {
        udelay(10);
        // Check lock status
        if (readl(base + PLL_LOCK_STATUS_ADDR) & 0x1) {
            break;
        }
    }
    // Enable single channel output
    writel(0x1, base + PLL_OUTPUT_CTRL_ADDR);
    
    dev_info(&clkdev->pdev->dev, "PLL configured successfully\n");
    
    return 0;
}

static int dynclk_probe(struct platform_device *pdev) {
    struct dynclk_dev *clkdev;
    int ret = 0;

    clkdev = kzalloc(sizeof(*clkdev), GFP_KERNEL);
    if (!clkdev)
        return -ENOMEM;

    clkdev->pdev = pdev;
    clkdev->base = devm_platform_ioremap_resource(pdev, 0);
    if (IS_ERR(clkdev->base)) {
        dev_err(&pdev->dev, "Failed to get base address\n");
        ret = PTR_ERR(clkdev->base);
        goto out;
    }

    ret = of_property_read_u32(pdev->dev.of_node, "refclk", &clkdev->refclk);
    if (ret != 0) {
        dev_err(&pdev->dev, "Failed to read refclk property\n");
        goto out;
    }

    ret = of_property_read_u32(pdev->dev.of_node, "expclk", &clkdev->expclk);
    if (ret != 0) {
        dev_err(&pdev->dev, "Failed to read expclk property\n");
        goto out;
    }

    dev_info(&pdev->dev, "Dynamic clock driver: input=%u Hz, output=%u Hz\n",
             clkdev->refclk, clkdev->expclk);

    ret = dynclk_set(clkdev);

out:
    kfree(clkdev);
    return ret;
}

static int dynclk_remove(struct platform_device *pdev) {
    return 0;
}

static void dynclk_shutdown(struct platform_device *pdev) {
    // PLL shutdown logic
}

static const struct of_device_id dynclk_of_match_table[] = {
    { .compatible = "anlogic,dynclk", },
    { /* end of table */ },
};

static struct platform_driver anlogic_dynclk_driver = {
    .probe = dynclk_probe,
    .remove = dynclk_remove,
    .shutdown = dynclk_shutdown,
    .driver = {
        .name = "anlogic-dynclk",
        .of_match_table = dynclk_of_match_table,
    },
};

module_platform_driver(anlogic_dynclk_driver);

MODULE_DESCRIPTION("Anlogic dynamic clock driver - Simplified Fractional Mode");
MODULE_AUTHOR("Guanghao.zhang <guanghao.zhang@anlogic.com>");
MODULE_LICENSE("GPL");
