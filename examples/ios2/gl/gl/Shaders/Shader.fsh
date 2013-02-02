//
//  Shader.fsh
//  gl
//
//  Created by Li Wei on 13-2-2.
//  Copyright (c) 2013年 Li Wei. All rights reserved.
//

varying lowp vec4 colorVarying;

void main()
{
    gl_FragColor = colorVarying;
}
